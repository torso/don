#include "config.h"
#if USE_POSIX_SPAWN
#include <spawn.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "cache.h"
#include "env.h"
#include "fail.h"
#include "file.h"
#include "hash.h"
#include "heap.h"
#include "log.h"
#include "native.h"
#include "pipe.h"
#include "stringpool.h"
#include "work.h"
#include "vm.h"

#define NATIVE_FUNCTION_COUNT 21

typedef vref (*invoke)(VM*);

typedef struct
{
    vref name;
    invoke function;
    uint parameterCount;
    uint returnValueCount;
} FunctionInfo;

static FunctionInfo functionInfo[NATIVE_FUNCTION_COUNT];
static uint initFunctionIndex = 1;


static const FunctionInfo *getFunctionInfo(nativefunctionref function)
{
    assert(function);
    assert(uintFromRef(function) < NATIVE_FUNCTION_COUNT);
    return (FunctionInfo*)&functionInfo[sizeFromRef(function)];
}

static bool addStringsLength(vref collection, uint *count, size_t *size)
{
    size_t index;
    vref value;
    for (index = 0; VCollectionGet(collection, VBoxSize(index++), &value);)
    {
        if (value == VFuture)
        {
            return false;
        }
        if (VIsCollection(value))
        {
            if (!addStringsLength(value, count, size))
            {
                return false;
            }
        }
        else
        {
            *size += VStringLength(value) + 1 + sizeof(char*);
            (*count)++;
        }
    }
    return true;
}

static void writeStrings(vref collection, char ***table, char **stringData)
{
    size_t index;
    vref value;
    for (index = 0; VCollectionGet(collection, VBoxSize(index++), &value);)
    {
        if (VIsCollection(value))
        {
            writeStrings(value, table, stringData);
        }
        else
        {
            **table = *stringData;
            *stringData = VWriteString(value, *stringData);
            **stringData = 0;
            (*table)++;
            (*stringData)++;
        }
    }
}

static char **createStringArray(vref collection)
{
    size_t size = sizeof(char*);
    uint count = 1;
    char **strings;
    char **table;
    char *stringData;

    assert(VIsCollection(collection));
    assert(VCollectionSize(collection));

    if (!addStringsLength(collection, &count, &size))
    {
        return null;
    }

    strings = (char**)malloc(size);

    table = strings;
    stringData = (char*)&strings[count];
    writeStrings(collection, &table, &stringData);
    *table = null;
    return strings;
}

static vref readFile(vref object, vref valueIfNotExists)
{
    const char *path;
    size_t pathLength;
    File file;
    vref string;
    char *data;
    size_t size;

    path = HeapGetPath(object, &pathLength);
    if (valueIfNotExists)
    {
        if (!FileTryOpen(&file, path, pathLength))
        {
            return valueIfNotExists;
        }
    }
    else
    {
        FileOpen(&file, path, pathLength);
    }
    size = FileSize(&file);
    if (!size)
    {
        FileClose(&file);
        return VEmptyString;
    }
    string = VCreateUninitialisedString(size, &data);
    FileRead(&file, (byte*)data, size);
    FileClose(&file);
    return string;
}

static int startProcess(const char *executable, char *const argv[],
                        const char *const envp[], int fdOut, int fdErr)
{
    pid_t pid;
    int status;
#if USE_POSIX_SPAWN
    posix_spawn_file_actions_t psfa;
    posix_spawn_file_actions_init(&psfa);
    posix_spawn_file_actions_adddup2(&psfa, fdOut, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&psfa, fdErr, STDERR_FILENO);
    status = posix_spawn(&pid, executable, &psfa, null, argv, (char*const*)envp);
    posix_spawn_file_actions_destroy(&psfa);
    if (status)
    {
        FailErrno(false);
    }
#else
    pid = VFORK();
    if (!pid)
    {
        status = dup2(fdOut, STDOUT_FILENO);
        if (status == -1)
        {
            FailErrno(true);
        }
        status = dup2(fdErr, STDERR_FILENO);
        if (status == -1)
        {
            FailErrno(true);
        }

        execve(executable, argv, (char*const*)envp);
        _exit(EXIT_FAILURE);
    }
#endif
    return pid;
}


static vref nativeCp(VM *vm)
{
    vref src = VMReadValue(vm);
    vref dst = VMReadValue(vm);
    const char *srcPath;
    const char *dstPath;
    size_t srcLength;
    size_t dstLength;

    if (src == VFuture || dst == VFuture || vm->base.parent)
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    srcPath = HeapGetPath(src, &srcLength);
    dstPath = HeapGetPath(dst, &dstLength);
    FileCopy(srcPath, srcLength, dstPath, dstLength);
    return 0;
}

static vref nativeEcho(VM *vm)
{
    vref message = VMReadValue(vm);
    vref prefix = VMReadValue(vm);
    if (!vm->base.parent)
    {
        if (prefix != VNull)
        {
            /* TODO: Avoid malloc */
            size_t length = VStringLength(prefix);
            char *buffer = (char*)malloc(length);
            VWriteString(prefix, buffer);
            LogSetPrefix(buffer, length);
            LogPrintObjectAutoNewline(message);
            LogSetPrefix(null, 0);
            free(buffer);
        }
        else
        {
            LogPrintObjectAutoNewline(message);
        }
    }
    return 0;
}

typedef struct
{
    vref command;
    vref env;
    vref echoOut;
    vref echoErr;
    vref fail;
} ExecEnv;

typedef struct
{
    vref outputStd;
    vref outputErr;
    vref exitcode;
} ExecReturn;

static vref workExec(Work *work, vref *values)
{
    ExecEnv *env = (ExecEnv*)values;
    ExecReturn execReturn;
    char *executable;
    vref value;
    char **argv;
    size_t arg0Length;
    const char *const*envp;
    size_t index;
    const char *path;
    pid_t pid;
    int status;
    int fdOut;
    int fdErr;
    Pipe out;
    Pipe err;
    size_t length;

    if (env->command == VFuture || env->env == VFuture ||
        env->echoOut == VFuture || env->echoErr == VFuture ||
        env->fail == VFuture || work->modifiedFiles == VFuture)
    {
        return 0;
    }

    assert(VCollectionSize(env->env) % 2 == 0);

    argv = createStringArray(env->command);
    if (!argv)
    {
        return 0;
    }

    arg0Length = strlen(argv[0]);
    executable = FileSearchPath(argv[0], arg0Length, &length, true);
    if (!executable)
    {
        executable = FileSearchPath(argv[0], arg0Length, &length, false);
        if (executable)
        {
            VMFailf(work->vm, "File is not executable: %s", executable);
            free(executable);
        }
        else
        {
            VMFailf(work->vm, "Command not found: %s", argv[0]);
        }
        free(argv);
        return 0;
    }

    fdOut = PipeInit(&out);
    fdErr = PipeInit(&err);

    envp = VCollectionSize(env->env) ? EnvCreateCopy(env->env) : EnvGetEnv();

    pid = startProcess(executable, argv, envp, fdOut, fdErr);
    free(executable);
    free(argv);
    if (VCollectionSize(env->env))
    {
        free((void*)envp);
    }
    close(fdOut);
    close(fdErr);
    if (pid < 0)
    {
        FailOOM();
    }

    for (index = 0; VCollectionGet(work->modifiedFiles, VBoxSize(index++), &value);)
    {
        assert(value != VFuture);
        path = HeapGetPath(value, &length);
        FileMarkModified(path, length);
    }

    if (VIsTruthy(env->echoOut))
    {
        PipeAddListener(&out, &LogPipeOutListener);
    }
    if (VIsTruthy(env->echoErr))
    {
        PipeAddListener(&err, &LogPipeOutListener);
    }
    PipeConsume2(&out, &err);

    pid = waitpid(pid, &status, 0);
    if (pid < 0)
    {
        FailErrno(false);
    }
    if (WEXITSTATUS(status) && VIsTruthy(env->fail))
    {
        PipeDispose(&out);
        PipeDispose(&err);
        VMFailf(work->vm, "Process exited with status %d", WEXITSTATUS(status));
        return 0;
    }
    execReturn.exitcode = VBoxInteger(WEXITSTATUS(status));
    execReturn.outputStd = VCreateString((const char*)BVGetPointer(&out.buffer, 0),
                                   BVSize(&out.buffer));
    PipeDispose(&out);
    execReturn.outputErr = VCreateString((const char*)BVGetPointer(&err.buffer, 0),
                                   BVSize(&err.buffer));
    PipeDispose(&err);
    LogAutoNewline();
    return VCreateArrayFromData((const vref*)&execReturn, 3);
}

static vref nativeExec(VM *vm)
{
    ExecEnv env;
    vref access, modify;

    env.command = VMReadValue(vm);
    env.env = VMReadValue(vm);
    env.echoOut = VMReadValue(vm);
    env.echoErr = VMReadValue(vm);
    env.fail = VMReadValue(vm);
    access = VMReadValue(vm);
    modify = VMReadValue(vm);

    vm->work = WorkAdd(workExec, vm, (const vref*)&env,
                       sizeof(ExecEnv) / sizeof(vref), access, modify);
    return VFuture;
}

static vref nativeFail(VM *vm)
{
    VMHalt(vm, VMReadValue(vm));
    return 0;
}

static vref nativeFile(VM *vm)
{
    vref path = VMReadValue(vm);
    vref name = VMReadValue(vm);
    vref extension = VMReadValue(vm);

    if (path == VFuture || name == VFuture || extension == VFuture)
    {
        return VFuture;
    }
    return HeapPathFromParts(path, name, extension);
}

static vref nativeFilename(VM *vm)
{
    vref path = VMReadValue(vm);
    const char *s;
    size_t length;

    if (path == VFuture)
    {
        return VFuture;
    }
    s = HeapGetPath(path, &length);
    s = FileStripPath(s, &length);
    return VCreateString(s, length);
}

/* TODO: Remove duplicate files. */
static vref nativeFilelist(VM *vm)
{
    vref value = VMReadValue(vm);
    if (value == VFuture)
    {
        return VFuture;
    }
    return HeapCreateFilelist(value);
}

typedef struct
{
    vref cacheFile;
    vref uptodate;
    vref data;
} GetCacheResult;

static vref nativeGetCache(VM *vm)
{
    vref key = VMReadValue(vm);
    vref echoCachedOutput = VMReadValue(vm);
    char *cachePath;
    size_t cachePathLength;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    bool uptodate;
    vref value;
    GetCacheResult result;

    if (key == VFuture || echoCachedOutput == VFuture ||
        vm->base.parent) /* TODO */
    {
        vm->idle = true;
        return 0;
    }
    HashInit(&hashState);
    HeapHash(key, &hashState);
    HashFinal(&hashState, hash);
    CacheGet(hash, VIsTruthy(echoCachedOutput),
             &uptodate, &cachePath, &cachePathLength, &value);
    result.cacheFile = HeapCreatePath(VCreateString(cachePath, cachePathLength));
    free(cachePath);
    result.uptodate = uptodate ? VTrue : VFalse;
    result.data = value;
    return VCreateArrayFromData((vref*)&result, 3);
}

static vref nativeGetEnv(VM *vm)
{
    vref name = VMReadValue(vm);
    size_t nameLength;
    char *buffer;
    const char *value;
    size_t valueLength;

    if (name == VFuture)
    {
        return VFuture;
    }

    nameLength = VStringLength(name);
    buffer = (char*)malloc(nameLength + 1);
    *VWriteString(name, buffer) = 0;
    EnvGet(buffer, nameLength, &value, &valueLength);
    free(buffer);
    return value ? VCreateString(value, valueLength) : VNull;
}

static vref nativeIndexOf(VM *vm)
{
    vref data = VMReadValue(vm);
    vref element = VMReadValue(vm);
    if (data == VFuture || element == VFuture)
    {
        return VFuture;
    }
    /* TODO: Support collections */
    assert(VIsString(data));
    assert(VIsString(element));
    return VStringIndexOf(data, 0, element);
}

static vref nativeLines(VM *vm)
{
    vref value = VMReadValue(vm);
    vref trimLastIfEmpty = VMReadValue(vm);
    vref content;

    if (value == VFuture || trimLastIfEmpty == VFuture ||
        (vm->base.parent && HeapIsFile(value))) /* TODO */
    {
        vm->idle = true;
        return 0;
    }

    content = HeapIsFile(value) ? readFile(value, 0) : value;
    assert(VIsString(content));
    return HeapSplit(content, VNewline, false, VIsTruthy(trimLastIfEmpty));
}

static vref nativeMv(VM *vm)
{
    vref src = VMReadValue(vm);
    vref dst = VMReadValue(vm);
    const char *oldPath;
    const char *newPath;
    size_t oldLength;
    size_t newLength;

    if (src == VFuture || dst == VFuture || vm->base.parent)
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    oldPath = HeapGetPath(src, &oldLength);
    newPath = HeapGetPath(dst, &newLength);
    FileRename(oldPath, oldLength, newPath, newLength);
    return 0;
}

static vref nativePid(VM *vm unused)
{
    return VBoxInteger(getpid());
}

static vref nativeReadFile(VM *vm)
{
    vref file = VMReadValue(vm);
    vref valueIfNotExists = VMReadValue(vm);

    if (file == VFuture || valueIfNotExists == VFuture || vm->base.parent)
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    return readFile(file, valueIfNotExists);
}

typedef struct
{
    vref result;
    vref count;
} ReplaceResult;

static vref nativeReplace(VM *vm)
{
    vref data = VMReadValue(vm);
    vref original = VMReadValue(vm);
    vref replacement = VMReadValue(vm);
    size_t dataLength;
    size_t originalLength;
    size_t replacementLength;
    size_t offset;
    size_t newOffset;
    vref offsetRef;
    char *p;
    uint replacements = 0;
    ReplaceResult result;

    if (data == VFuture || original == VFuture || replacement == VFuture)
    {
        return VFuture;
    }

    dataLength = VStringLength(data);
    originalLength = VStringLength(original);
    replacementLength = VStringLength(replacement);
    if (originalLength)
    {
        for (offset = 0;; offset++)
        {
            offsetRef = VStringIndexOf(data, offset, original);
            if (offsetRef == VNull)
            {
                break;
            }
            replacements++;
            offset = VUnboxSize(offsetRef);
        }
    }
    if (!replacements)
    {
        result.result = data;
        result.count = VBoxInteger(0);
        return VCreateArrayFromData((vref*)&result, 2);
    }
    result.result = VCreateUninitialisedString(
        dataLength + replacements * (replacementLength - originalLength), &p);
    result.count = VBoxUint(replacements);
    offset = 0;
    while (replacements--)
    {
        newOffset = VUnboxSize(VStringIndexOf(data, offset, original));
        p = VWriteSubstring(data, offset, newOffset - offset, p);
        p = VWriteString(replacement, p);
        offset = newOffset + originalLength;
    }
    VWriteSubstring(data, offset, dataLength - offset, p);
    return VCreateArrayFromData((vref*)&result, 2);
}

static vref nativeRm(VM *vm)
{
    vref file = VMReadValue(vm);
    const char *path;
    size_t length;

    if (file == VFuture || vm->base.parent)
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    path = HeapGetPath(file, &length);
    FileDelete(path, length);
    return 0;
}

static vref nativeSetUptodate(VM *vm)
{
    vref cacheFile = VMReadValue(vm);
    vref out = VMReadValue(vm);
    vref err = VMReadValue(vm);
    vref data = VMReadValue(vm);
    vref accessedFiles = VMReadValue(vm);
    const char *path;
    size_t length;

    if (vm->base.parent)
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    path = HeapGetPath(cacheFile, &length);
    CacheSetUptodate(path, length, accessedFiles, out, err, data);
    return 0;
}

static vref nativeSize(VM *vm)
{
    vref value = VMReadValue(vm);

    if (value == VFuture)
    {
        return VFuture;
    }

    if (VIsCollection(value))
    {
        assert(VCollectionSize(value) <= INT_MAX);
        return VBoxSize(VCollectionSize(value));
    }
    else if (likely(VIsString(value)))
    {
        return VBoxSize(VStringLength(value));
    }

    {
        const char msg[] = "Argument to size must be an array or string";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

static vref nativeSplit(VM *vm)
{
    vref value = VMReadValue(vm);
    vref delimiter = VMReadValue(vm);
    vref removeEmpty = VMReadValue(vm);
    vref data;

    if (value == VFuture || delimiter == VFuture ||
        removeEmpty == VFuture || (vm->base.parent && HeapIsFile(value)))
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    data = HeapIsFile(value) ? readFile(value, 0) : value;
    assert(VIsString(data));
    assert(VIsString(delimiter) || VIsCollection(delimiter));
    return HeapSplit(data, delimiter, VIsTruthy(removeEmpty), false);
}

static vref nativeWriteFile(VM *vm)
{
    vref file = VMReadValue(vm);
    vref data = VMReadValue(vm);
    const char *path;
    size_t pathLength;
    File f;
    byte buffer[1024];
    size_t offset = 0;
    size_t size;

    if (file == VFuture || data == VFuture || vm->base.parent)
    {
        /* TODO */
        vm->idle = true;
        return 0;
    }

    size = VStringLength(data);
    path = HeapGetPath(file, &pathLength);
    FileOpenAppend(&f, path, pathLength, true);
    while (size)
    {
        size_t chunkSize = min(size, sizeof(buffer));
        VWriteSubstring(data, offset, chunkSize, (char*)buffer);
        FileWrite(&f, buffer, chunkSize);
        offset += chunkSize;
        size -= chunkSize;
    }
    FileClose(&f);
    return 0;
}


static void addFunctionInfo(const char *name, invoke function,
                            uint parameterCount, uint returnValueCount)
{
    assert(parameterCount + returnValueCount <= NATIVE_MAX_VALUES);
    functionInfo[initFunctionIndex].name = StringPoolAdd(name);
    functionInfo[initFunctionIndex].function = function;
    functionInfo[initFunctionIndex].parameterCount = parameterCount;
    functionInfo[initFunctionIndex].returnValueCount = returnValueCount;
    initFunctionIndex++;
}

void NativeInit(void)
{
    addFunctionInfo("cp",          nativeCp,          2, 0);
    addFunctionInfo("echo",        nativeEcho,        2, 0);
    addFunctionInfo("exec",        nativeExec,        7, 3);
    addFunctionInfo("fail",        nativeFail,        1, 0);
    addFunctionInfo("file",        nativeFile,        3, 1);
    addFunctionInfo("filename",    nativeFilename,    1, 1);
    addFunctionInfo("filelist",    nativeFilelist,    1, 1);
    addFunctionInfo("getCache",    nativeGetCache,    2, 3);
    addFunctionInfo("getEnv",      nativeGetEnv,      1, 1);
    addFunctionInfo("indexOf",     nativeIndexOf,     2, 1);
    addFunctionInfo("lines",       nativeLines,       2, 1);
    addFunctionInfo("mv",          nativeMv,          2, 0);
    addFunctionInfo("pid",         nativePid,         0, 1);
    addFunctionInfo("readFile",    nativeReadFile,    2, 1);
    addFunctionInfo("replace",     nativeReplace,     3, 2);
    addFunctionInfo("rm",          nativeRm,          1, 0);
    addFunctionInfo("setUptodate", nativeSetUptodate, 5, 0);
    addFunctionInfo("size",        nativeSize,        1, 1);
    addFunctionInfo("split",       nativeSplit,       3, 1);
    addFunctionInfo("writeFile",   nativeWriteFile,   2, 0);
}

vref NativeInvoke(VM *vm, nativefunctionref function)
{
    return getFunctionInfo(function)->function(vm);
}

nativefunctionref NativeFindFunction(vref name)
{
    uint i;
    for (i = 1; i < NATIVE_FUNCTION_COUNT; i++)
    {
        if (NativeGetName(refFromUint(i)) == name)
        {
            return refFromUint(i);
        }
    }
    return 0;
}

vref NativeGetName(nativefunctionref function)
{
    return getFunctionInfo(function)->name;
}

uint NativeGetParameterCount(nativefunctionref function)
{
    return getFunctionInfo(function)->parameterCount;
}

uint NativeGetReturnValueCount(nativefunctionref function)
{
    return getFunctionInfo(function)->returnValueCount;
}
