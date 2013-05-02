#define _XOPEN_SOURCE 600
#include <memory.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "cache.h"
#include "env.h"
#include "fail.h"
#include "fieldindex.h"
#include "file.h"
#include "hash.h"
#include "namespace.h"
#include "native.h"
#include "pipe.h"
#include "log.h"
#include "stringpool.h"
#include "value.h"
#include "work.h"

#define NATIVE_FUNCTION_COUNT 19

typedef boolean (*preInvoke)(void*);
typedef boolean (*invoke)(void*);

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

static boolean addStringsLength(vref collection, uint *count, size_t *size)
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
            *size += HeapStringLength(value) + 1 + sizeof(char*);
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
            *stringData = HeapWriteString(value, *stringData);
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
    assert(HeapCollectionSize(collection));

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

static boolean appendFiles(vref value, intvector *result)
{
    vref o;
    size_t index;

    if (HeapIsFutureValue(value))
    {
        return false;
    }

    if (HeapIsCollection(value))
    {
        for (index = 0;
             HeapCollectionGet(value, HeapBoxSize(index++), &o);)
        {
            if (!appendFiles(o, result))
            {
                return false;
            }
        }
    }
    else
    {
        IVAddRef(result, HeapCreatePath(value));
    }
    return true;
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

static boolean nativeCp(CpEnv *env)
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

static boolean nativeEcho(EchoEnv *env)
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
        length = HeapStringLength(env->prefix);
        buffer = (char*)malloc(length);
        HeapWriteString(env->prefix, buffer);
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
        env->output = HeapCreateArray(output, 2);
    }
}

static boolean nativeExec(ExecEnv *env)
{
    char *executable;
    vref value;
    char **argv;
    const char *const*envp;
    size_t index;
    const char *path;
    pid_t pid;
    int status;
    int fdOut[2];
    int fdErr[2];
    Pipe out;
    Pipe err;
    size_t length;
#ifdef HAVE_POSIX_SPAWN
    posix_spawn_file_actions_t psfa;
#endif

    assert(HeapCollectionSize(env->env) % 2 == 0);

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

    status = pipe(fdOut);
    if (status == -1)
    {
        FailErrno(false);
    }
    status = pipe(fdErr);
    if (status == -1)
    {
        FailErrno(false);
    }

    envp = HeapCollectionSize(env->env) ? EnvCreateCopy(env->env) : EnvGetEnv();

    assert(!HeapIsFutureValue(env->work.modifiedFiles));
    for (index = 0; HeapCollectionGet(env->work.modifiedFiles,
                                      HeapBoxSize(index++), &value);)
    {
        assert(!HeapIsFutureValue(value));
        path = HeapGetPath(value, &length);
        FileMarkModified(path, length);
    }

#ifdef HAVE_POSIX_SPAWN
    posix_spawn_file_actions_init(&psfa);
    posix_spawn_file_actions_addclose(&psfa, fdOut[0]);
    posix_spawn_file_actions_addclose(&psfa, fdErr[0]);
    posix_spawn_file_actions_adddup2(&psfa, fdOut[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&psfa, fdErr[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&psfa, fdOut[1]);
    posix_spawn_file_actions_addclose(&psfa, fdErr[1]);
    status = posix_spawn(&pid, executable, &psfa, null, argv, (char**)envp);
    posix_spawn_file_actions_destroy(&psfa);
    if (status)
    {
        FailErrno(false);
    }
#else
    pid = fork();
    if (!pid)
    {
        close(fdOut[0]);
        close(fdErr[0]);

        status = dup2(fdOut[1], STDOUT_FILENO);
        if (status == -1)
        {
            FailErrno(true);
        }
        close(fdOut[1]);
        status = dup2(fdErr[1], STDERR_FILENO);
        if (status == -1)
        {
            FailErrno(true);
        }
        close(fdErr[1]);

        execve(executable, argv, (char**)envp);
        _exit(EXIT_FAILURE);
    }
#endif
    free(executable);
    free(argv);
    if (HeapCollectionSize(env->env))
    {
        free((void*)envp);
    }
    close(fdOut[1]);
    close(fdErr[1]);
    if (pid < 0)
    {
        FailOOM();
    }

    PipeInitFD(&out, fdOut[0]);
    PipeInitFD(&err, fdErr[0]);
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

static boolean nativeFail(FailEnv *env)
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

static boolean nativeFile(FileEnv *env)
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

static boolean nativeFilename(FilenameEnv *env)
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
} FilesetEnv;

/* TODO: Remove duplicate files. */
static boolean nativeFileset(FilesetEnv *env)
{
    boolean status;
    intvector files;

    IVInit(&files, 16);
    status = appendFiles(env->value, &files);
    env->result = HeapCreateArrayFromVector(&files);
    IVDispose(&files);
    return status;
}

typedef struct
{
    Work work;

    vref key;
    vref echoCachedOutput;

    vref cacheFile;
    vref uptodate;
} GetCacheEnv;

static boolean nativeGetCache(GetCacheEnv *env)
{
    cacheref ref;
    const char *cachePath;
    size_t cachePathLength;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    boolean uptodate;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->key) || HeapIsFutureValue(env->echoCachedOutput))
    {
        return false;
    }

    HashInit(&hashState);
    HeapHash(env->key, &hashState);
    HashFinal(&hashState, hash);
    ref = CacheGet(hash);
    uptodate = CacheCheckUptodate(ref);
    cachePath = CacheGetFile(ref, &cachePathLength);
    env->cacheFile = HeapCreatePath(HeapCreateString(cachePath,
                                                     cachePathLength));
    if (uptodate)
    {
        env->uptodate = HeapTrue;
        if (VIsTruthy(env->echoCachedOutput))
        {
            CacheEchoCachedOutput(ref);
        }
    }
    else
    {
        env->uptodate = HeapFalse;
        FileMkdir(cachePath, cachePathLength);
    }
    return true;
}

typedef struct
{
    Work work;

    vref name;

    vref result;
} GetEnvEnv;

static boolean nativeGetEnv(GetEnvEnv *env)
{
    char *buffer;
    size_t nameLength;
    const char *value;
    size_t valueLength;

    if (HeapIsFutureValue(env->name))
    {
        return false;
    }

    nameLength = HeapStringLength(env->name);
    buffer = (char*)malloc(nameLength + 1);
    *HeapWriteString(env->name, buffer) = 0;
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

static boolean nativeIndexOf(IndexOfEnv *env)
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

static boolean nativeLines(LinesEnv *env)
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
    env->work.modifiedFiles = HeapCreateArray(files, 2);
}

static boolean nativeMv(MvEnv *env)
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

    vref file;

    vref result;
} ReadFileEnv;

static void nativePreReadFile(ReadFileEnv *env)
{
    env->work.accessedFiles = env->file;
}

static boolean nativeReadFile(ReadFileEnv *env)
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

static boolean nativeReplace(ReplaceEnv *env)
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

    dataLength = HeapStringLength(env->data);
    originalLength = HeapStringLength(env->original);
    replacementLength = HeapStringLength(env->replacement);
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
        p = HeapWriteString(env->replacement, p);
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

static boolean nativeRm(RmEnv *env)
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
    vref accessedFiles;
} SetUptodateEnv;

static void nativePreSetUptodate(SetUptodateEnv *env)
{
    /* Marking the cache file as accessed should prevent the entry from being
     * marked uptodate before all previous commands on it has completed. */
    env->work.accessedFiles = env->cacheFile;
}

static boolean nativeSetUptodate(SetUptodateEnv *env)
{
    cacheref ref;
    vref value;
    size_t index;
    const char *path;
    size_t length;
    size_t outLength;
    size_t errLength;
    char *output = null;

    if (!VIsTruthy(env->work.condition) ||
        HeapIsFutureValue(env->cacheFile) || HeapIsFutureValue(env->out) ||
        HeapIsFutureValue(env->err) || HeapIsFutureValue(env->accessedFiles))
    {
        return false;
    }

    path = HeapGetPath(env->cacheFile, &length);
    ref = CacheGetFromFile(path, length);
    outLength = HeapStringLength(env->out);
    errLength = HeapStringLength(env->err);
    if (env->accessedFiles)
    {
        for (index = 0; HeapCollectionGet(env->accessedFiles,
                                          HeapBoxSize(index++), &value);)
        {
            assert(!HeapIsFutureValue(value)); /* TODO: Don't assume fileset is always finished. */
            path = HeapGetPath(value, &length);
            CacheAddDependency(ref, path, length);
        }
    }
    if (outLength || errLength)
    {
        output = (char*)malloc(outLength + errLength);
        HeapWriteString(env->out, output);
        HeapWriteString(env->err, output + outLength);
    }
    /* TODO: Sync files in cache directory. */
    CacheSetUptodate(ref, outLength, errLength, output);
    return true;
}

typedef struct
{
    Work work;

    vref value;

    vref result;
} SizeEnv;

static boolean nativeSize(SizeEnv *env)
{
    if (HeapIsFutureValue(env->value))
    {
        return false;
    }

    if (HeapIsCollection(env->value))
    {
        assert(HeapCollectionSize(env->value) <= INT_MAX);
        env->result = HeapBoxSize(HeapCollectionSize(env->value));
    }
    else
    {
        assert(HeapIsString(env->value));
        env->result = HeapBoxSize(HeapStringLength(env->value));
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

static boolean nativeSplit(SplitEnv *env)
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
    addFunctionInfo("fileset",     null,                            (invoke)nativeFileset,     1, 1);
    addFunctionInfo("getCache",    null,                            (invoke)nativeGetCache,    2, 2);
    addFunctionInfo("getEnv",      null,                            (invoke)nativeGetEnv,      1, 1);
    addFunctionInfo("indexOf",     null,                            (invoke)nativeIndexOf,     2, 1);
    addFunctionInfo("lines",       (preInvoke)nativePreLines,       (invoke)nativeLines,       2, 1);
    addFunctionInfo("mv",          (preInvoke)nativePreMv,          (invoke)nativeMv,          2, 0);
    addFunctionInfo("readFile",    (preInvoke)nativePreReadFile,    (invoke)nativeReadFile,    1, 1);
    addFunctionInfo("replace",     null,                            (invoke)nativeReplace,     3, 2);
    addFunctionInfo("rm",          (preInvoke)nativePreRm,          (invoke)nativeRm,          1, 0);
    addFunctionInfo("setUptodate", (preInvoke)nativePreSetUptodate, (invoke)nativeSetUptodate, 4, 0);
    addFunctionInfo("size",        null,                            (invoke)nativeSize,        1, 1);
    addFunctionInfo("split",       (preInvoke)nativePreSplit,       (invoke)nativeSplit,       3, 1);
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
    VMPopMany(vm, env.values, info->parameterCount);
    memset(env.values + info->parameterCount, 0,
           info->returnValueCount * sizeof(*env.values));
    for (i = info->parameterCount, p = env.values; i; i--, p++)
    {
        *p = HeapTryWait(*p);
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
    VMPushMany(vm, env.values + info->parameterCount, info->returnValueCount);
}

void NativeWork(Work *work)
{
    boolean finished = getFunctionInfo(work->function)->function(work);
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
