#include "common.h"
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "vm.h"
#include "bytecode.h"
#include "cache.h"
#include "env.h"
#include "fail.h"
#include "file.h"
#include "hash.h"
#include "namespace.h"
#include "native.h"
#include "pipe.h"
#include "log.h"
#include "stringpool.h"
#include "work.h"

#define NATIVE_FUNCTION_COUNT 21

typedef bool (*preInvoke)(void*);
typedef bool (*invoke)(void*);

typedef struct
{
    vref name;
    preInvoke preFunction;
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
    for (index = 0; HeapCollectionGet(collection, HeapBoxSize(index++), &value);)
    {
        if (HeapIsFutureValue(value))
        {
            return false;
        }
        if (HeapIsCollection(value))
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
    for (index = 0; HeapCollectionGet(collection, HeapBoxSize(index++), &value);)
    {
        if (HeapIsCollection(value))
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

    assert(HeapIsCollection(collection));
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

static vref readFile(vref object)
{
    const char *path;
    size_t pathLength;
    File file;
    vref string;
    char *data;
    size_t size;

    path = HeapGetPath(object, &pathLength);
    FileOpen(&file, path, pathLength);
    size = FileSize(&file);
    if (!size)
    {
        FileClose(&file);
        return HeapEmptyString;
    }
    string = HeapCreateUninitialisedString(size, &data);
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


typedef struct
{
    Work work;

    vref src;
    vref dst;
} CpEnv;

static void nativePreCp(CpEnv *env)
{
    env->work.accessedFiles = env->src;
    env->work.modifiedFiles = env->dst;
}

static bool nativeCp(CpEnv *env)
{
    const char *srcPath;
    const char *dstPath;
    size_t srcLength;
    size_t dstLength;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->src) || HeapIsFutureValue(env->dst))
    {
        return false;
    }

    srcPath = HeapGetPath(env->src, &srcLength);
    dstPath = HeapGetPath(env->dst, &dstLength);
    FileCopy(srcPath, srcLength, dstPath, dstLength);
    return true;
}

typedef struct
{
    Work work;

    vref message;
    vref prefix;
} EchoEnv;

static bool nativeEcho(EchoEnv *env)
{
    char *buffer;
    size_t length;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->message) || HeapIsFutureValue(env->prefix))
    {
        return false;
    }
    if (env->prefix)
    {
        /* TODO: Avoid malloc */
        length = VStringLength(env->prefix);
        buffer = (char*)malloc(length);
        VWriteString(env->prefix, buffer);
        LogSetPrefix(buffer, length);
        LogPrintObjectAutoNewline(env->message);
        LogSetPrefix(null, 0);
        free(buffer);
    }
    else
    {
        LogPrintObjectAutoNewline(env->message);
    }
    return true;
}

typedef struct
{
    Work work;

    vref command;
    vref env;
    vref echoOut;
    vref echoErr;
    vref access;
    vref modify;

    vref output;
    vref exitcode;
    vref internalError;
} ExecEnv;

static void nativePreExec(ExecEnv *env)
{
    vref output[2];

    env->work.accessedFiles = env->access;
    env->work.modifiedFiles = env->modify;

    if (!env->output)
    {
        output[0] = HeapCreateFutureValue();
        output[1] = HeapCreateFutureValue();
        env->output = HeapCreateArrayFromData(output, 2);
    }
}

static bool nativeExec(ExecEnv *env)
{
    char *executable;
    vref value;
    char **argv;
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

    assert(VCollectionSize(env->env) % 2 == 0);

    argv = createStringArray(env->command);
    if (!argv)
    {
        return false;
    }

    env->internalError = HeapBoxInteger(0);
    executable = FileSearchPath(argv[0], strlen(argv[0]), &length, true);
    if (!executable)
    {
        executable = FileSearchPath(argv[0], strlen(argv[0]), &length, false);
        if (executable)
        {
            free(executable);
            env->exitcode = HeapBoxInteger(126);
            env->internalError = HeapBoxInteger(2);
        }
        else
        {
            env->exitcode = HeapBoxInteger(127);
            env->internalError = HeapBoxInteger(1);
        }
        free(argv);
        HeapCollectionGet(env->output, HeapBoxInteger(0), &value);
        HeapSetFutureValue(value, HeapEmptyString);
        HeapCollectionGet(env->output, HeapBoxInteger(1), &value);
        HeapSetFutureValue(value, HeapEmptyString);
        return true;
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

    assert(!HeapIsFutureValue(env->work.modifiedFiles));
    for (index = 0; HeapCollectionGet(env->work.modifiedFiles,
                                      HeapBoxSize(index++), &value);)
    {
        assert(!HeapIsFutureValue(value));
        path = HeapGetPath(value, &length);
        FileMarkModified(path, length);
    }

    if (VIsTruthy(env->echoOut))
    {
        PipeAddListener(&out, &LogPipeOutListener);
    }
    if (VIsTruthy(env->echoErr))
    {
        PipeAddListener(&err, &LogPipeErrListener);
    }
    PipeConsume2(&out, &err);

    pid = waitpid(pid, &status, 0);
    if (pid < 0)
    {
        FailErrno(false);
    }
    env->exitcode = HeapBoxInteger(WEXITSTATUS(status));
    HeapCollectionGet(env->output, HeapBoxInteger(0), &value);
    HeapSetFutureValue(value, HeapCreateString(
                           (const char*)BVGetPointer(&out.buffer, 0),
                           BVSize(&out.buffer)));
    PipeDispose(&out);
    HeapCollectionGet(env->output, HeapBoxInteger(1), &value);
    HeapSetFutureValue(value, HeapCreateString(
                           (const char*)BVGetPointer(&err.buffer, 0),
                           BVSize(&err.buffer)));
    PipeDispose(&err);
    LogAutoNewline();
    LogErrAutoNewline();
    return true;
}

typedef struct
{
    Work work;

    vref message;
} FailEnv;

static bool nativeFail(FailEnv *env)
{
    if (!VIsTruthy(env->work.condition))
    {
        env->work.vm->ip = null;
        return false;
    }
    if (env->message)
    {
        LogPrintErrObjectAutoNewline(env->message);
    }
    FailVM(env->work.vm);
}

typedef struct
{
    Work work;

    vref path;
    vref name;
    vref extension;

    vref result;
} FileEnv;

static bool nativeFile(FileEnv *env)
{
    if (HeapIsFutureValue(env->path) || HeapIsFutureValue(env->name) ||
        HeapIsFutureValue(env->extension))
    {
        return false;
    }

    env->result = HeapPathFromParts(env->path, env->name, env->extension);
    return true;
}

typedef struct
{
    Work work;

    vref path;

    vref result;
} FilenameEnv;

static bool nativeFilename(FilenameEnv *env)
{
    const char *s;
    size_t length;

    if (HeapIsFutureValue(env->path))
    {
        return false;
    }

    s = HeapGetPath(env->path, &length);
    s = FileStripPath(s, &length);
    env->result = HeapCreateString(s, length);
    return true;
}

typedef struct
{
    Work work;

    vref value;

    vref result;
} FilelistEnv;

/* TODO: Remove duplicate files. */
static bool nativeFilelist(FilelistEnv *env)
{
    env->result = HeapCreateFilelist(env->value);
    return true;
}

typedef struct
{
    Work work;

    vref key;
    vref echoCachedOutput;

    vref cacheFile;
    vref uptodate;
    vref data;
} GetCacheEnv;

static bool nativeGetCache(GetCacheEnv *env)
{
    char *cachePath;
    size_t cachePathLength;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    bool uptodate;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->key) || HeapIsFutureValue(env->echoCachedOutput))
    {
        return false;
    }

    HashInit(&hashState);
    HeapHash(env->key, &hashState);
    HashFinal(&hashState, hash);
    CacheGet(hash, VIsTruthy(env->echoCachedOutput),
             &uptodate, &cachePath, &cachePathLength, &env->data);
    env->cacheFile = HeapCreatePath(HeapCreateString(cachePath,
                                                     cachePathLength));
    env->uptodate = uptodate ? HeapTrue : HeapFalse;
    free(cachePath);
    return true;
}

typedef struct
{
    Work work;

    vref name;

    vref result;
} GetEnvEnv;

static bool nativeGetEnv(GetEnvEnv *env)
{
    char *buffer;
    size_t nameLength;
    const char *value;
    size_t valueLength;

    if (HeapIsFutureValue(env->name))
    {
        return false;
    }

    nameLength = VStringLength(env->name);
    buffer = (char*)malloc(nameLength + 1);
    *VWriteString(env->name, buffer) = 0;
    EnvGet(buffer, nameLength, &value, &valueLength);
    free(buffer);
    env->result = value ? HeapCreateString(value, valueLength) : 0;
    return true;
}

typedef struct
{
    Work work;

    vref data;
    vref element;

    vref result;
} IndexOfEnv;

static bool nativeIndexOf(IndexOfEnv *env)
{
    if (HeapIsFutureValue(env->data) || HeapIsFutureValue(env->element))
    {
        return false;
    }

    /* TODO: Support collections */
    assert(HeapIsString(env->data));
    assert(HeapIsString(env->element));
    env->result = HeapStringIndexOf(env->data, 0, env->element);
    return true;
}

typedef struct
{
    Work work;

    vref value;
    vref trimLastIfEmpty;

    vref result;
} LinesEnv;

static void nativePreLines(LinesEnv *env)
{
    if (HeapIsFutureValue(env->value) || HeapIsFile(env->value))
    {
        env->work.accessedFiles = env->value;
    }
}

static bool nativeLines(LinesEnv *env)
{
    vref content;

    if (HeapIsFutureValue(env->value) ||
        HeapIsFutureValue(env->trimLastIfEmpty))
    {
        return false;
    }

    content = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(content));
    env->result = HeapSplit(content, HeapNewline, false,
                            VIsTruthy(env->trimLastIfEmpty));
    return true;
}

typedef struct
{
    Work work;

    vref src;
    vref dst;
} MvEnv;

static void nativePreMv(MvEnv *env)
{
    vref files[2];
    files[0] = env->src;
    files[1] = env->dst;
    /* TODO: Don't reallocate array if it exists. */
    env->work.modifiedFiles = HeapCreateArrayFromData(files, 2);
}

static bool nativeMv(MvEnv *env)
{
    const char *oldPath;
    const char *newPath;
    size_t oldLength;
    size_t newLength;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->src) || HeapIsFutureValue(env->dst))
    {
        return false;
    }

    oldPath = HeapGetPath(env->src, &oldLength);
    newPath = HeapGetPath(env->dst, &newLength);
    FileRename(oldPath, oldLength, newPath, newLength);
    return true;
}

typedef struct
{
    Work work;
    vref result;
} PidEnv;

static bool nativePid(PidEnv *env)
{
    env->result = HeapBoxInteger(getpid());
    return true;
}

typedef struct
{
    Work work;

    vref file;

    vref result;
} ReadFileEnv;

static void nativePreReadFile(ReadFileEnv *env)
{
    env->work.accessedFiles = env->file;
}

static bool nativeReadFile(ReadFileEnv *env)
{
    if (!VIsTruthy(env->work.condition) || HeapIsFutureValue(env->file))
    {
        return false;
    }
    env->result = readFile(env->file);
    return true;
}

typedef struct
{
    Work work;

    vref data;
    vref original;
    vref replacement;

    vref result;
    vref count;
} ReplaceEnv;

static bool nativeReplace(ReplaceEnv *env)
{
    size_t dataLength;
    size_t originalLength;
    size_t replacementLength;
    size_t offset;
    size_t newOffset;
    vref offsetRef;
    char *p;
    uint replacements = 0;

    if (HeapIsFutureValue(env->data) || HeapIsFutureValue(env->original) ||
        HeapIsFutureValue(env->replacement))
    {
        return false;
    }

    dataLength = VStringLength(env->data);
    originalLength = VStringLength(env->original);
    replacementLength = VStringLength(env->replacement);
    if (originalLength)
    {
        for (offset = 0;; offset++)
        {
            offsetRef = HeapStringIndexOf(env->data, offset, env->original);
            if (!offsetRef)
            {
                break;
            }
            replacements++;
            offset = HeapUnboxSize(offsetRef);
        }
    }
    if (!replacements)
    {
        env->result = env->data;
        env->count = HeapBoxInteger(0);
        return true;
    }
    env->result = HeapCreateUninitialisedString(
        dataLength + replacements * (replacementLength - originalLength),
        &p);
    env->count = HeapBoxUint(replacements);
    offset = 0;
    while (replacements--)
    {
        newOffset = HeapUnboxSize(HeapStringIndexOf(env->data, offset, env->original));
        p = HeapWriteSubstring(env->data, offset, newOffset - offset, p);
        p = VWriteString(env->replacement, p);
        offset = newOffset + originalLength;
    }
    HeapWriteSubstring(env->data, offset, dataLength - offset, p);
    return true;
}

typedef struct
{
    Work work;

    vref file;
} RmEnv;

static void nativePreRm(RmEnv *env)
{
    env->work.modifiedFiles = env->file;
}

static bool nativeRm(RmEnv *env)
{
    const char *path;
    size_t length;

    if (!VIsTruthy(env->work.condition) || HeapIsFutureValue(env->file))
    {
        return false;
    }

    path = HeapGetPath(env->file, &length);
    FileDelete(path, length);
    return true;
}

typedef struct
{
    Work work;

    vref cacheFile;
    vref out;
    vref err;
    vref data;
    vref accessedFiles;
} SetUptodateEnv;

static void nativePreSetUptodate(SetUptodateEnv *env)
{
    /* Marking the cache file as accessed should prevent the entry from being
     * marked uptodate before all previous commands on it has completed. */
    env->work.accessedFiles = env->cacheFile;
}

static bool nativeSetUptodate(SetUptodateEnv *env)
{
    const char *path;
    size_t length;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->cacheFile) || HeapIsFutureValue(env->out) ||
        HeapIsFutureValue(env->err) || HeapIsFutureValue(env->data) ||
        HeapIsFutureValue(env->accessedFiles))
    {
        return false;
    }

    path = HeapGetPath(env->cacheFile, &length);
    CacheSetUptodate(path, length, env->accessedFiles, env->out, env->err, env->data);
    return true;
}

typedef struct
{
    Work work;

    vref value;

    vref result;
} SizeEnv;

static bool nativeSize(SizeEnv *env)
{
    if (HeapIsFutureValue(env->value))
    {
        return false;
    }

    if (HeapIsCollection(env->value))
    {
        assert(VCollectionSize(env->value) <= INT_MAX);
        env->result = HeapBoxSize(VCollectionSize(env->value));
    }
    else
    {
        assert(HeapIsString(env->value));
        env->result = HeapBoxSize(VStringLength(env->value));
    }
    return true;
}

typedef struct
{
    Work work;

    vref value;
    vref delimiter;
    vref removeEmpty;

    vref result;
} SplitEnv;

static void nativePreSplit(SplitEnv *env)
{
    env->work.accessedFiles = env->value;
}

static bool nativeSplit(SplitEnv *env)
{
    vref data;

    if (HeapIsFutureValue(env->value) ||
        HeapIsFutureValue(env->delimiter) ||
        HeapIsFutureValue(env->removeEmpty))
    {
        return false;
    }

    data = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(data));
    assert(HeapIsString(env->delimiter));
    env->result = HeapSplit(data, env->delimiter, VIsTruthy(env->removeEmpty),
                            false);
    return true;
}

typedef struct
{
    Work work;

    vref file;
    vref data;
} WriteFileEnv;

static void nativePreWriteFile(WriteFileEnv *env)
{
    env->work.modifiedFiles = env->file;
}

static bool nativeWriteFile(WriteFileEnv *env)
{
    const char *path;
    size_t pathLength;
    File file;
    byte buffer[1024];
    size_t offset = 0;
    size_t size;

    if (!VIsTruthy(env->work.condition) || HeapIsFutureValue(env->file) ||
        HeapIsFutureValue(env->data))
    {
        return false;
    }

    size = VStringLength(env->data);
    path = HeapGetPath(env->file, &pathLength);
    FileOpenAppend(&file, path, pathLength, true);
    while (size)
    {
        size_t chunkSize = min(size, sizeof(buffer));
        HeapWriteSubstring(env->data, offset, chunkSize, (char*)buffer);
        FileWrite(&file, buffer, chunkSize);
        offset += chunkSize;
        size -= chunkSize;
    }
    FileClose(&file);
    return true;
}


static void addFunctionInfo(const char *name,
                            preInvoke preFunction, invoke function,
                            uint parameterCount, uint returnValueCount)
{
    assert(parameterCount + returnValueCount <= NATIVE_MAX_VALUES);
    functionInfo[initFunctionIndex].name = StringPoolAdd(name);
    functionInfo[initFunctionIndex].preFunction = preFunction;
    functionInfo[initFunctionIndex].function = function;
    functionInfo[initFunctionIndex].parameterCount = parameterCount;
    functionInfo[initFunctionIndex].returnValueCount = returnValueCount;
    initFunctionIndex++;
}

void NativeInit(void)
{
    addFunctionInfo("cp",          (preInvoke)nativePreCp,          (invoke)nativeCp,          2, 0);
    addFunctionInfo("echo",        null,                            (invoke)nativeEcho,        2, 0);
    addFunctionInfo("exec",        (preInvoke)nativePreExec,        (invoke)nativeExec,        6, 3);
    addFunctionInfo("fail",        null,                            (invoke)nativeFail,        1, 0);
    addFunctionInfo("file",        null,                            (invoke)nativeFile,        3, 1);
    addFunctionInfo("filename",    null,                            (invoke)nativeFilename,    1, 1);
    addFunctionInfo("filelist",    null,                            (invoke)nativeFilelist,    1, 1);
    addFunctionInfo("getCache",    null,                            (invoke)nativeGetCache,    2, 3);
    addFunctionInfo("getEnv",      null,                            (invoke)nativeGetEnv,      1, 1);
    addFunctionInfo("indexOf",     null,                            (invoke)nativeIndexOf,     2, 1);
    addFunctionInfo("lines",       (preInvoke)nativePreLines,       (invoke)nativeLines,       2, 1);
    addFunctionInfo("mv",          (preInvoke)nativePreMv,          (invoke)nativeMv,          2, 0);
    addFunctionInfo("pid",         null,                            (invoke)nativePid,         0, 1);
    addFunctionInfo("readFile",    (preInvoke)nativePreReadFile,    (invoke)nativeReadFile,    1, 1);
    addFunctionInfo("replace",     null,                            (invoke)nativeReplace,     3, 2);
    addFunctionInfo("rm",          (preInvoke)nativePreRm,          (invoke)nativeRm,          1, 0);
    addFunctionInfo("setUptodate", (preInvoke)nativePreSetUptodate, (invoke)nativeSetUptodate, 5, 0);
    addFunctionInfo("size",        null,                            (invoke)nativeSize,        1, 1);
    addFunctionInfo("split",       (preInvoke)nativePreSplit,       (invoke)nativeSplit,       3, 1);
    addFunctionInfo("writeFile",   (preInvoke)nativePreWriteFile,   (invoke)nativeWriteFile,   2, 0);
}

void NativeInvoke(VM *vm, nativefunctionref function)
{
    const FunctionInfo *info = getFunctionInfo(function);
    vref *p;
    uint i;
    struct
    {
        Work work;
        vref values[NATIVE_MAX_VALUES];
    } env;

    assert(info->parameterCount + info->returnValueCount <= NATIVE_MAX_VALUES);
    env.work.vm = vm;
    env.work.condition = vm->condition;
    env.work.accessedFiles = 0;
    env.work.modifiedFiles = 0;
    memset(env.values + info->parameterCount, 0,
           info->returnValueCount * sizeof(*env.values));
    for (i = 0; i < info->parameterCount; i++)
    {
        env.values[i] = HeapTryWait(VMReadValue(vm));
    }
    if (info->preFunction)
    {
        info->preFunction(&env);
    }
    if (env.work.accessedFiles || env.work.modifiedFiles ||
        !info->function(&env))
    {
        for (i = info->returnValueCount, p = env.values + info->parameterCount;
             i;
             i--, p++)
        {
            if (!*p)
            {
                *p = HeapCreateFutureValue();
            }
        }
        env.work.function = function;
        WorkAdd(&env.work);
    }
    for (i = 0; i < info->returnValueCount; i++)
    {
        VMStoreValue(vm, *vm->ip++, env.values[info->parameterCount + i]);
    }
}

void NativeWork(Work *work)
{
    bool finished = getFunctionInfo(work->function)->function(work);
    assert(finished);
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
