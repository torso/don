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
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "pipe.h"
#include "stringpool.h"
#include "work.h"

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
    for (index = 0; VCollectionGet(collection, HeapBoxSize(index++), &value);)
    {
        if (HeapIsFutureValue(value))
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
    for (index = 0; VCollectionGet(collection, HeapBoxSize(index++), &value);)
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
    vref src;
    vref dst;
} CpEnv;

static bool workCp(Work *work, vref *values)
{
    CpEnv *env = (CpEnv*)values;
    const char *srcPath;
    const char *dstPath;
    size_t srcLength;
    size_t dstLength;

    env->src = HeapTryWait(env->src);
    env->dst = HeapTryWait(env->dst);
    if (!VIsTruthy(work->condition) ||
        HeapIsFutureValue(env->src) || HeapIsFutureValue(env->dst))
    {
        return false;
    }

    srcPath = HeapGetPath(env->src, &srcLength);
    dstPath = HeapGetPath(env->dst, &dstLength);
    FileCopy(srcPath, srcLength, dstPath, dstLength);
    return true;
}

static vref nativeCp(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workCp, vm, sizeof(CpEnv) / sizeof(vref), &values);
    CpEnv *env = (CpEnv*)values;

    env->src = VMReadValue(vm);
    env->dst = VMReadValue(vm);
    if (!workCp(work, values))
    {
        work->accessedFiles = env->src;
        work->modifiedFiles = env->dst;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return 0;
}

typedef struct
{
    vref message;
    vref prefix;
} EchoEnv;

static bool workEcho(Work *work, vref *values)
{
    EchoEnv *env = (EchoEnv*)values;

    env->message = HeapTryWait(env->message);
    env->prefix = HeapTryWait(env->prefix);
    if (!VIsTruthy(work->condition) ||
        HeapIsFutureValue(env->message) || HeapIsFutureValue(env->prefix))
    {
        return false;
    }
    if (env->prefix != HeapNull)
    {
        /* TODO: Avoid malloc */
        size_t length = VStringLength(env->prefix);
        char *buffer = (char*)malloc(length);
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

static vref nativeEcho(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workEcho, vm, sizeof(EchoEnv) / sizeof(vref), &values);
    EchoEnv *env = (EchoEnv*)values;

    env->message = VMReadValue(vm);
    env->prefix = VMReadValue(vm);
    if (!workEcho(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return 0;
}

typedef struct
{
    vref command;
    vref env;
    vref echoOut;
    vref echoErr;
    vref access;
    vref modify;

    vref outputStd;
    vref outputErr;
    vref exitcode;
    vref internalError;
} ExecEnv;

static bool workExec(Work *work, vref *values)
{
    ExecEnv *env = (ExecEnv*)values;
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

    env->command = HeapTryWait(env->command);
    env->env = HeapTryWait(env->env);
    env->echoOut = HeapTryWait(env->echoOut);
    env->echoErr = HeapTryWait(env->echoErr);
    env->access = HeapTryWait(env->access);
    env->modify = HeapTryWait(env->modify);
    if (HeapIsFutureValue(work->condition) || HeapIsFutureValue(env->command) ||
        HeapIsFutureValue(env->env) || HeapIsFutureValue(env->echoOut) ||
        HeapIsFutureValue(env->echoErr) || HeapIsFutureValue(env->access) ||
        HeapIsFutureValue(env->modify))
    {
        return false;
    }

    assert(VCollectionSize(env->env) % 2 == 0);

    argv = createStringArray(env->command);
    if (!argv)
    {
        return false;
    }

    executable = FileSearchPath(argv[0], strlen(argv[0]), &length, true);
    if (!executable)
    {
        executable = FileSearchPath(argv[0], strlen(argv[0]), &length, false);
        if (executable)
        {
            free(executable);
            HeapSetFutureValue(env->exitcode, HeapBoxInteger(126));
            HeapSetFutureValue(env->internalError, HeapBoxInteger(2));
        }
        else
        {
            HeapSetFutureValue(env->exitcode, HeapBoxInteger(127));
            HeapSetFutureValue(env->internalError, HeapBoxInteger(1));
        }
        free(argv);
        HeapSetFutureValue(env->outputStd, HeapEmptyString);
        HeapSetFutureValue(env->outputErr, HeapEmptyString);
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

    for (index = 0; VCollectionGet(env->modify, HeapBoxSize(index++), &value);)
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
        PipeAddListener(&err, &LogPipeOutListener);
    }
    PipeConsume2(&out, &err);

    pid = waitpid(pid, &status, 0);
    if (pid < 0)
    {
        FailErrno(false);
    }
    HeapSetFutureValue(env->internalError, HeapBoxInteger(0));
    HeapSetFutureValue(env->exitcode, HeapBoxInteger(WEXITSTATUS(status)));
    HeapSetFutureValue(env->outputStd, HeapCreateString(
                           (const char*)BVGetPointer(&out.buffer, 0),
                           BVSize(&out.buffer)));
    PipeDispose(&out);
    HeapSetFutureValue(env->outputErr, HeapCreateString(
                           (const char*)BVGetPointer(&err.buffer, 0),
                           BVSize(&err.buffer)));
    PipeDispose(&err);
    LogAutoNewline();
    return true;
}

static vref nativeExec(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workExec, vm, sizeof(ExecEnv) / sizeof(vref), &values);
    ExecEnv *env = (ExecEnv*)values;

    env->command = VMReadValue(vm);
    env->env = VMReadValue(vm);
    env->echoOut = VMReadValue(vm);
    env->echoErr = VMReadValue(vm);
    env->access = VMReadValue(vm);
    env->modify = VMReadValue(vm);

    env->outputStd = HeapCreateFutureValue();
    env->outputErr = HeapCreateFutureValue();
    env->exitcode = HeapCreateFutureValue();
    env->internalError = HeapCreateFutureValue();

    if (!workExec(work, values))
    {
        work->accessedFiles = env->access;
        work->modifiedFiles = env->modify;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return VCreateArrayFromData(&env->outputStd, 4);
}

typedef struct
{
    vref message;
} FailEnv;

static bool workFail(Work *work, vref *values)
{
    FailEnv *env = (FailEnv*)values;

    env->message = HeapTryWait(env->message);
    if (HeapIsFutureValue(env->message))
    {
        return false;
    }
    assert(HeapIsString(env->message));
    VMFail(work->vm, null, "%s", HeapGetStringCopy(env->message));
    return true;
}

static vref nativeFail(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workFail, vm, sizeof(FailEnv) / sizeof(vref), &values);
    FailEnv *env = (FailEnv*)values;

    env->message = VMReadValue(vm);
    vm->ip = null;
    if (!workFail(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return 0;
}

typedef struct
{
    vref path;
    vref name;
    vref extension;

    vref result;
} FileEnv;

static bool workFile(Work *work unused, vref *values)
{
    FileEnv *env = (FileEnv*)values;

    env->path = HeapTryWait(env->path);
    env->name = HeapTryWait(env->name);
    env->extension = HeapTryWait(env->extension);
    if (HeapIsFutureValue(env->path) || HeapIsFutureValue(env->name) ||
        HeapIsFutureValue(env->extension))
    {
        return false;
    }

    HeapSetFutureValue(env->result, HeapPathFromParts(env->path, env->name, env->extension));
    return true;
}

static vref nativeFile(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workFile, vm, sizeof(FileEnv) / sizeof(vref), &values);
    FileEnv *env = (FileEnv*)values;

    env->path = VMReadValue(vm);
    env->name = VMReadValue(vm);
    env->extension = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workFile(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref path;

    vref result;
} FilenameEnv;

static bool workFilename(Work *work unused, vref *values)
{
    FilenameEnv *env = (FilenameEnv*)values;
    const char *s;
    size_t length;

    env->path = HeapTryWait(env->path);
    if (HeapIsFutureValue(env->path))
    {
        return false;
    }

    s = HeapGetPath(env->path, &length);
    s = FileStripPath(s, &length);
    HeapSetFutureValue(env->result, HeapCreateString(s, length));
    return true;
}

static vref nativeFilename(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workFilename, vm, sizeof(FilenameEnv) / sizeof(vref), &values);
    FilenameEnv *env = (FilenameEnv*)values;

    env->path = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workFilename(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref value;

    vref result;
} FilelistEnv;

/* TODO: Remove duplicate files. */
static bool workFilelist(Work *work, vref *values)
{
    FilelistEnv *env = (FilelistEnv*)values;

    env->value = HeapTryWait(env->value);
    if (!VIsTruthy(work->condition) || HeapIsFutureValue(env->value))
    {
        return false;
    }

    HeapSetFutureValue(env->result, HeapCreateFilelist(env->value));
    return true;
}

static vref nativeFilelist(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workFilelist, vm, sizeof(FilelistEnv) / sizeof(vref), &values);
    FilelistEnv *env = (FilelistEnv*)values;

    env->value = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workFilelist(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref key;
    vref echoCachedOutput;

    vref cacheFile;
    vref uptodate;
    vref data;
} GetCacheEnv;

static bool workGetCache(Work *work, vref *values)
{
    GetCacheEnv *env = (GetCacheEnv*)values;
    char *cachePath;
    size_t cachePathLength;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    bool uptodate;
    vref value;

    env->key = HeapTryWait(env->key);
    env->echoCachedOutput = HeapTryWait(env->echoCachedOutput);
    if (!VIsTruthy(work->condition) ||
        HeapIsFutureValue(env->key) || HeapIsFutureValue(env->echoCachedOutput))
    {
        return false;
    }

    HashInit(&hashState);
    HeapHash(env->key, &hashState);
    HashFinal(&hashState, hash);
    CacheGet(hash, VIsTruthy(env->echoCachedOutput),
             &uptodate, &cachePath, &cachePathLength, &value);
    HeapSetFutureValue(env->data, value);
    HeapSetFutureValue(env->cacheFile,
                       HeapCreatePath(HeapCreateString(cachePath, cachePathLength)));
    HeapSetFutureValue(env->uptodate, uptodate ? HeapTrue : HeapFalse);
    free(cachePath);
    return true;
}

static vref nativeGetCache(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workGetCache, vm, sizeof(GetCacheEnv) / sizeof(vref), &values);
    GetCacheEnv *env = (GetCacheEnv*)values;

    env->key = VMReadValue(vm);
    env->echoCachedOutput = VMReadValue(vm);

    env->cacheFile = HeapCreateFutureValue();
    env->uptodate = HeapCreateFutureValue();
    env->data = HeapCreateFutureValue();
    if (!workGetCache(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return VCreateArrayFromData(&env->cacheFile, 3);
}

typedef struct
{
    vref name;

    vref result;
} GetEnvEnv;

static bool workGetEnv(Work *work unused, vref *values)
{
    GetEnvEnv *env = (GetEnvEnv*)values;
    char *buffer;
    size_t nameLength;
    const char *value;
    size_t valueLength;

    env->name = HeapTryWait(env->name);
    if (HeapIsFutureValue(env->name))
    {
        return false;
    }

    nameLength = VStringLength(env->name);
    buffer = (char*)malloc(nameLength + 1);
    *VWriteString(env->name, buffer) = 0;
    EnvGet(buffer, nameLength, &value, &valueLength);
    free(buffer);
    HeapSetFutureValue(env->result, value ? HeapCreateString(value, valueLength) : HeapNull);
    return true;
}

static vref nativeGetEnv(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workGetEnv, vm, sizeof(GetEnvEnv) / sizeof(vref), &values);
    GetEnvEnv *env = (GetEnvEnv*)values;

    env->name = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workGetEnv(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref data;
    vref element;

    vref result;
} IndexOfEnv;

static bool workIndexOf(Work *work unused, vref *values)
{
    IndexOfEnv *env = (IndexOfEnv*)values;

    env->data = HeapTryWait(env->data);
    env->element = HeapTryWait(env->element);
    if (HeapIsFutureValue(env->data) || HeapIsFutureValue(env->element))
    {
        return false;
    }

    /* TODO: Support collections */
    assert(HeapIsString(env->data));
    assert(HeapIsString(env->element));
    HeapSetFutureValue(env->result, HeapStringIndexOf(env->data, 0, env->element));
    return true;
}

static vref nativeIndexOf(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workIndexOf, vm, sizeof(IndexOfEnv) / sizeof(vref), &values);
    IndexOfEnv *env = (IndexOfEnv*)values;

    env->data = VMReadValue(vm);
    env->element = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workIndexOf(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref value;
    vref trimLastIfEmpty;

    vref result;
} LinesEnv;

static bool workLines(Work *work unused, vref *values)
{
    LinesEnv *env = (LinesEnv*)values;
    vref content;

    env->value = HeapTryWait(env->value);
    env->trimLastIfEmpty = HeapTryWait(env->trimLastIfEmpty);
    if (HeapIsFutureValue(env->value) ||
        HeapIsFutureValue(env->trimLastIfEmpty))
    {
        return false;
    }

    content = HeapIsFile(env->value) ? readFile(env->value, 0) : env->value;
    assert(HeapIsString(content));
    HeapSetFutureValue(env->result, HeapSplit(content, HeapNewline, false,
                                              VIsTruthy(env->trimLastIfEmpty)));
    return true;
}

static vref nativeLines(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workLines, vm, sizeof(LinesEnv) / sizeof(vref), &values);
    LinesEnv *env = (LinesEnv*)values;

    env->value = VMReadValue(vm);
    env->trimLastIfEmpty = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workLines(work, values))
    {
        if (HeapIsFutureValue(env->value) || HeapIsFile(env->value))
        {
            work->accessedFiles = env->value;
        }
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref src;
    vref dst;
} MvEnv;

static bool workMv(Work *work, vref *values)
{
    MvEnv *env = (MvEnv*)values;
    const char *oldPath;
    const char *newPath;
    size_t oldLength;
    size_t newLength;

    env->src = HeapTryWait(env->src);
    env->dst = HeapTryWait(env->dst);
    if (!VIsTruthy(work->condition) ||
        HeapIsFutureValue(env->src) || HeapIsFutureValue(env->dst))
    {
        return false;
    }

    oldPath = HeapGetPath(env->src, &oldLength);
    newPath = HeapGetPath(env->dst, &newLength);
    FileRename(oldPath, oldLength, newPath, newLength);
    return true;
}

static vref nativeMv(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workMv, vm, sizeof(MvEnv) / sizeof(vref), &values);
    MvEnv *env = (MvEnv*)values;

    env->src = VMReadValue(vm);
    env->dst = VMReadValue(vm);
    if (!workMv(work, values))
    {
        vref files[2];
        files[0] = env->src;
        files[1] = env->dst;
        /* TODO: Don't reallocate array if it exists. */
        work->modifiedFiles = VCreateArrayFromData(files, 2);
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return 0;
}

static vref nativePid(VM *vm unused)
{
    return HeapBoxInteger(getpid());
}

typedef struct
{
    vref file;
    vref valueIfNotExists;

    vref result;
} ReadFileEnv;

static bool workReadFile(Work *work, vref *values)
{
    ReadFileEnv *env = (ReadFileEnv*)values;

    env->file = HeapTryWait(env->file);
    env->valueIfNotExists = HeapTryWait(env->valueIfNotExists);
    if (!VIsTruthy(work->condition) || HeapIsFutureValue(env->file) ||
        HeapIsFutureValue(env->valueIfNotExists))
    {
        return false;
    }
    HeapSetFutureValue(env->result, readFile(env->file, env->valueIfNotExists));
    return true;
}

static vref nativeReadFile(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workReadFile, vm, sizeof(ReadFileEnv) / sizeof(vref), &values);
    ReadFileEnv *env = (ReadFileEnv*)values;

    env->file = VMReadValue(vm);
    env->valueIfNotExists = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workReadFile(work, values))
    {
        work->accessedFiles = env->file;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref data;
    vref original;
    vref replacement;

    vref result;
    vref count;
} ReplaceEnv;

static bool workReplace(Work *work unused, vref *values)
{
    ReplaceEnv *env = (ReplaceEnv*)values;
    size_t dataLength;
    size_t originalLength;
    size_t replacementLength;
    size_t offset;
    size_t newOffset;
    vref offsetRef;
    char *p;
    uint replacements = 0;

    env->data = HeapTryWait(env->data);
    env->original = HeapTryWait(env->original);
    env->replacement = HeapTryWait(env->replacement);
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
            if (offsetRef == HeapNull)
            {
                break;
            }
            replacements++;
            offset = HeapUnboxSize(offsetRef);
        }
    }
    if (!replacements)
    {
        HeapSetFutureValue(env->result, env->data);
        HeapSetFutureValue(env->count, HeapBoxInteger(0));
        return true;
    }
    HeapSetFutureValue(env->result, HeapCreateUninitialisedString(
                           dataLength + replacements * (replacementLength - originalLength), &p));
    HeapSetFutureValue(env->count, HeapBoxUint(replacements));
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

static vref nativeReplace(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workReplace, vm, sizeof(ReplaceEnv) / sizeof(vref), &values);
    ReplaceEnv *env = (ReplaceEnv*)values;

    env->data = VMReadValue(vm);
    env->original = VMReadValue(vm);
    env->replacement = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    env->count = HeapCreateFutureValue();
    if (!workReplace(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return VCreateArrayFromData(&env->result, 2);
}

typedef struct
{
    vref file;
} RmEnv;

static bool workRm(Work *work, vref *values)
{
    RmEnv *env = (RmEnv*)values;
    const char *path;
    size_t length;

    env->file = HeapTryWait(env->file);
    if (!VIsTruthy(work->condition) || HeapIsFutureValue(env->file))
    {
        return false;
    }

    path = HeapGetPath(env->file, &length);
    FileDelete(path, length);
    return true;
}

static vref nativeRm(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workRm, vm, sizeof(RmEnv) / sizeof(vref), &values);
    RmEnv *env = (RmEnv*)values;

    env->file = VMReadValue(vm);
    if (!workRm(work, values))
    {
        work->modifiedFiles = env->file;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return 0;
}

typedef struct
{
    vref cacheFile;
    vref out;
    vref err;
    vref data;
    vref accessedFiles;
} SetUptodateEnv;

static bool workSetUptodate(Work *work, vref *values)
{
    SetUptodateEnv *env = (SetUptodateEnv*)values;
    const char *path;
    size_t length;

    env->cacheFile = HeapTryWait(env->cacheFile);
    env->out = HeapTryWait(env->out);
    env->err = HeapTryWait(env->err);
    env->data = HeapTryWait(env->data);
    env->accessedFiles = HeapTryWait(env->accessedFiles);
    if (!VIsTruthy(work->condition) ||
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

static vref nativeSetUptodate(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workSetUptodate, vm, sizeof(SetUptodateEnv) / sizeof(vref), &values);
    SetUptodateEnv *env = (SetUptodateEnv*)values;

    env->cacheFile = VMReadValue(vm);
    env->out = VMReadValue(vm);
    env->err = VMReadValue(vm);
    env->data = VMReadValue(vm);
    env->accessedFiles = VMReadValue(vm);
    if (!workSetUptodate(work, values))
    {
        /* Marking the cache file as accessed should prevent the entry from being
         * marked uptodate before all previous commands on it has completed. */
        work->accessedFiles = env->cacheFile;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return 0;
}

typedef struct
{
    vref value;

    vref result;
} SizeEnv;

static bool workSize(Work *work, vref *values)
{
    SizeEnv *env = (SizeEnv*)values;
    vref result;

    env->value = HeapTryWait(env->value);
    if (HeapIsFutureValue(env->value))
    {
        return false;
    }

    if (VIsCollection(env->value))
    {
        assert(VCollectionSize(env->value) <= INT_MAX);
        result = HeapBoxSize(VCollectionSize(env->value));
    }
    else if (likely(HeapIsString(env->value)))
    {
        result = HeapBoxSize(VStringLength(env->value));
    }
    else
    {
        VMFail(work->vm, work->ip, "Argument to size must be an array or string");
        return false;
    }
    HeapSetFutureValue(env->result, result);
    return true;
}

static vref nativeSize(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workSize, vm, sizeof(SizeEnv) / sizeof(vref), &values);
    SizeEnv *env = (SizeEnv*)values;

    env->value = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workSize(work, values))
    {
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref value;
    vref delimiter;
    vref removeEmpty;

    vref result;
} SplitEnv;

static bool workSplit(Work *work unused, vref *values)
{
    SplitEnv *env = (SplitEnv*)values;
    vref data;

    env->value = HeapTryWait(env->value);
    env->delimiter = HeapTryWait(env->delimiter);
    env->removeEmpty = HeapTryWait(env->removeEmpty);
    if (HeapIsFutureValue(env->value) ||
        HeapIsFutureValue(env->delimiter) ||
        HeapIsFutureValue(env->removeEmpty))
    {
        return false;
    }

    data = HeapIsFile(env->value) ? readFile(env->value, 0) : env->value;
    assert(HeapIsString(data));
    assert(HeapIsString(env->delimiter) || VIsCollection(env->delimiter));
    HeapSetFutureValue(env->result,
                       HeapSplit(data, env->delimiter, VIsTruthy(env->removeEmpty), false));
    return true;
}

static vref nativeSplit(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workSplit, vm, sizeof(SplitEnv) / sizeof(vref), &values);
    SplitEnv *env = (SplitEnv*)values;

    env->value = VMReadValue(vm);
    env->delimiter = VMReadValue(vm);
    env->removeEmpty = VMReadValue(vm);
    env->result = HeapCreateFutureValue();
    if (!workSplit(work, values))
    {
        work->accessedFiles = env->value;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
    return env->result;
}

typedef struct
{
    vref file;
    vref data;
} WriteFileEnv;

static bool workWriteFile(Work *work, vref *values)
{
    WriteFileEnv *env = (WriteFileEnv*)values;
    const char *path;
    size_t pathLength;
    File file;
    byte buffer[1024];
    size_t offset = 0;
    size_t size;

    env->file = HeapTryWait(env->file);
    env->data = HeapTryWait(env->data);
    if (!VIsTruthy(work->condition) || HeapIsFutureValue(env->file) ||
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

static vref nativeWriteFile(VM *vm)
{
    vref *values;
    Work *work = WorkAdd(workWriteFile, vm, sizeof(WriteFileEnv) / sizeof(vref), &values);
    WriteFileEnv *env = (WriteFileEnv*)values;

    env->file = VMReadValue(vm);
    env->data = VMReadValue(vm);
    if (!workWriteFile(work, values))
    {
        work->modifiedFiles = env->file;
        WorkCommit(work);
    }
    else
    {
        WorkAbort(work);
    }
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
    addFunctionInfo("exec",        nativeExec,        6, 4);
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
