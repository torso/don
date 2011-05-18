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
#include "fieldindex.h"
#include "file.h"
#include "hash.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "stringpool.h"
#include "task.h"
#include "work.h"

#define NATIVE_FUNCTION_COUNT 19

typedef boolean (*preInvoke)(void*);
typedef boolean (*invoke)(void*);

typedef struct
{
    stringref name;
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

static char **createStringArray(VM *vm, objectref collection)
{
    Iterator iter;
    objectref value;
    size_t size = sizeof(char*);
    uint count = 1;
    char **strings;
    char **table;
    char *stringData;

    assert(HeapIsCollection(collection));
    assert(HeapCollectionSize(collection));

    HeapIteratorInit(&iter, collection, true);
    while (HeapIteratorNext(vm, &iter, &value))
    {
        if (HeapIsFutureValue(value))
        {
            return null;
        }
        size += HeapStringLength(value) + 1 + sizeof(char*);
        count++;
    }

    strings = (char**)malloc(size);

    table = strings;
    stringData = (char*)&strings[count];
    HeapIteratorInit(&iter, collection, true);
    while (HeapIteratorNext(vm, &iter, &value))
    {
        *table++ = stringData;
        stringData = HeapWriteString(value, stringData);
        *stringData++ = 0;
    }
    *table = null;
    return strings;
}

static objectref readFile(objectref object)
{
    const char *path;
    size_t pathLength;
    File file;
    objectref string;
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

    objectref src;
    objectref dst;
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

    if (env->work.condition != HeapTrue ||
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

    objectref message;
    objectref prefix;
} EchoEnv;

static boolean nativeEcho(EchoEnv *env)
{
    char *buffer;
    size_t length;

    if (env->work.condition != HeapTrue ||
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

    objectref command;
    objectref env;
    objectref echoOut;
    objectref echoErr;
    objectref access;
    objectref modify;

    objectref output;
    objectref status;
} ExecEnv;

static void nativePreExec(ExecEnv *env)
{
    objectref output[2];

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
    objectref value;
    char **argv;
    const char *const*envp;
    Iterator iter;
    const char *path;
    pid_t pid;
    int status;
    int pipeOut[2];
    int pipeErr[2];
    const byte *p;
    size_t length;
#ifdef HAVE_POSIX_SPAWN
    posix_spawn_file_actions_t psfa;
#endif

    assert(HeapCollectionSize(env->env) % 2 == 0);

    argv = createStringArray(env->work.vm, env->command);
    if (!argv)
    {
        return false;
    }

    executable = FileSearchPath(argv[0], strlen(argv[0]), &length);
    if (!executable)
    {
        HeapCollectionGet(env->output, HeapBoxInteger(0), &value);
        HeapSetFutureValue(value, HeapEmptyString);
        HeapCollectionGet(env->output, HeapBoxInteger(1), &value);
        HeapSetFutureValue(value, HeapEmptyString);
        env->status = HeapBoxInteger(-1);
        return true;
    }

    status = pipe(pipeOut);
    if (status == -1)
    {
        TaskFailErrno(false);
    }
    status = pipe(pipeErr);
    if (status == -1)
    {
        TaskFailErrno(false);
    }

    envp = HeapCollectionSize(env->env) ?
        EnvCreateCopy(env->work.vm, env->env) : EnvGetEnv();

    assert(!HeapIsFutureValue(env->work.modifiedFiles));
    HeapIteratorInit(&iter, env->work.modifiedFiles, false);
    while (HeapIteratorNext(env->work.vm, &iter, &value))
    {
        assert(!HeapIsFutureValue(value));
        path = HeapGetPath(value, &length);
        FileMarkModified(path, length);
    }

#ifdef HAVE_POSIX_SPAWN
    posix_spawn_file_actions_init(&psfa);
    posix_spawn_file_actions_addclose(&psfa, pipeOut[0]);
    posix_spawn_file_actions_addclose(&psfa, pipeErr[0]);
    posix_spawn_file_actions_adddup2(&psfa, pipeOut[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&psfa, pipeErr[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&psfa, pipeOut[1]);
    posix_spawn_file_actions_addclose(&psfa, pipeErr[1]);
    status = posix_spawn(&pid, executable, &psfa, null, argv, (char**)envp);
    posix_spawn_file_actions_destroy(&psfa);
    if (status)
    {
        TaskFailErrno(false);
    }
#else
    pid = fork();
    if (!pid)
    {
        close(pipeOut[0]);
        close(pipeErr[0]);

        status = dup2(pipeOut[1], STDOUT_FILENO);
        if (status == -1)
        {
            TaskFailErrno(true);
        }
        close(pipeOut[1]);
        status = dup2(pipeErr[1], STDERR_FILENO);
        if (status == -1)
        {
            TaskFailErrno(true);
        }
        close(pipeErr[1]);

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
    close(pipeOut[1]);
    close(pipeErr[1]);
    if (pid < 0)
    {
        TaskFailOOM();
    }

    LogPushOutBuffer(HeapIsTrue(env->echoOut));
    LogPushErrBuffer(HeapIsTrue(env->echoErr));
    LogConsumePipes(pipeOut[0], pipeErr[0]);

    pid = waitpid(pid, &status, 0);
    if (pid < 0)
    {
        TaskFailErrno(false);
    }
    env->status = HeapBoxInteger(WEXITSTATUS(status));
    LogGetOutBuffer(&p, &length);
    HeapCollectionGet(env->output, HeapBoxInteger(0), &value);
    HeapSetFutureValue(value, HeapCreateString((const char*)p, length));
    LogPopOutBuffer();
    LogGetErrBuffer(&p, &length);
    HeapCollectionGet(env->output, HeapBoxInteger(1), &value);
    HeapSetFutureValue(value, HeapCreateString((const char*)p, length));
    LogPopErrBuffer();
    LogAutoNewline();
    LogErrAutoNewline();
    return true;
}

typedef struct
{
    Work work;

    objectref message;
} FailEnv;

static boolean nativeFail(FailEnv *env)
{
    if (env->work.condition != HeapTrue)
    {
        env->work.vm->ip = null;
        return false;
    }
    if (env->message)
    {
        LogPrintErrObjectAutoNewline(env->message);
    }
    TaskFailVM(env->work.vm);
}

typedef struct
{
    Work work;

    objectref path;
    objectref name;
    objectref extension;

    objectref result;
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

    objectref path;

    objectref result;
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

    objectref value;

    objectref result;
} FilesetEnv;

/* TODO: Remove duplicate files. */
static boolean nativeFileset(FilesetEnv *env)
{
    objectref o;
    intvector files;
    Iterator iter;

    if (HeapIsFutureValue(env->value))
    {
        return false;
    }

    if (HeapIsCollection(env->value))
    {
        IntVectorInit(&files);
        HeapIteratorInit(&iter, env->value, true);
        while (HeapIteratorNext(env->work.vm, &iter, &o))
        {
            if (HeapIsFutureValue(o))
            {
                return false;
            }
            o = HeapCreatePath(o);
            IntVectorAddRef(&files, o);
        }
        /* TODO: Reuse collection if possible. */
        env->result = HeapCreateArrayFromVector(&files);
        IntVectorDispose(&files);
    }
    else
    {
        o = HeapCreatePath(env->value);
        env->result = HeapCreateArray(&o, 1);
    }
    return true;
}

typedef struct
{
    Work work;

    objectref key;
    objectref echoCachedOutput;

    objectref cacheFile;
    objectref uptodate;
} GetCacheEnv;

static boolean nativeGetCache(GetCacheEnv *env)
{
    cacheref ref;
    const char *cachePath;
    size_t cachePathLength;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    boolean uptodate;

    if (env->work.condition != HeapTrue ||
        HeapIsFutureValue(env->key) || HeapIsFutureValue(env->echoCachedOutput))
    {
        return false;
    }

    HashInit(&hashState);
    HeapHash(env->work.vm, env->key, &hashState);
    HashFinal(&hashState, hash);
    ref = CacheGet(hash);
    uptodate = CacheCheckUptodate(ref);
    cachePath = CacheGetFile(ref, &cachePathLength);
    env->cacheFile = HeapCreatePath(HeapCreateString(cachePath,
                                                     cachePathLength));
    if (uptodate)
    {
        env->uptodate = HeapTrue;
        if (HeapIsTrue(env->echoCachedOutput))
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

    objectref name;

    objectref result;
} GetenvEnv;

static boolean nativeGetenv(GetenvEnv *env)
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

    objectref data;
    objectref element;

    objectref result;
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

    objectref value;
    objectref trimEmptyLastLine;

    objectref result;
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
    objectref content;

    if (HeapIsFutureValue(env->value) || HeapIsFutureValue(env->trimEmptyLastLine))
    {
        return false;
    }

    content = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(content));
    env->result = HeapSplit(content, HeapNewline, false,
                            HeapIsTrue(env->trimEmptyLastLine));
    return true;
}

typedef struct
{
    Work work;

    objectref src;
    objectref dst;
} MvEnv;

static void nativePreMv(MvEnv *env)
{
    objectref files[2];
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

    if (env->work.condition != HeapTrue ||
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

    objectref file;

    objectref result;
} ReadFileEnv;

static void nativePreReadFile(ReadFileEnv *env)
{
    env->work.accessedFiles = env->file;
}

static boolean nativeReadFile(ReadFileEnv *env)
{
    if (env->work.condition != HeapTrue || HeapIsFutureValue(env->file))
    {
        return false;
    }
    env->result = readFile(env->file);
    return true;
}

typedef struct
{
    Work work;

    objectref data;
    objectref original;
    objectref replacement;

    objectref result;
    objectref count;
} ReplaceEnv;

static boolean nativeReplace(ReplaceEnv *env)
{
    size_t dataLength;
    size_t originalLength;
    size_t replacementLength;
    size_t offset;
    size_t newOffset;
    objectref offsetRef;
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

    objectref file;
} RmEnv;

static void nativePreRm(RmEnv *env)
{
    env->work.modifiedFiles = env->file;
}

static boolean nativeRm(RmEnv *env)
{
    const char *path;
    size_t length;

    if (env->work.condition != HeapTrue || HeapIsFutureValue(env->file))
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

    objectref cacheFile;
    objectref out;
    objectref err;
    objectref accessedFiles;
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
    objectref value;
    Iterator iter;
    const char *path;
    size_t length;
    size_t outLength;
    size_t errLength;
    char *output = null;

    if (env->work.condition != HeapTrue ||
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
        HeapIteratorInit(&iter, env->accessedFiles, true);
        while (HeapIteratorNext(env->work.vm, &iter, &value))
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

    objectref value;

    objectref result;
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

    objectref value;
    objectref delimiter;
    objectref removeEmpty;

    objectref result;
} SplitEnv;

static void nativePreSplit(SplitEnv *env)
{
    env->work.accessedFiles = env->value;
}

static boolean nativeSplit(SplitEnv *env)
{
    objectref data;

    if (HeapIsFutureValue(env->value) ||
        HeapIsFutureValue(env->delimiter) ||
        HeapIsFutureValue(env->removeEmpty))
    {
        return false;
    }

    data = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(data));
    assert(HeapIsString(env->delimiter));
    env->result = HeapSplit(data, env->delimiter, HeapIsTrue(env->removeEmpty),
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
    addFunctionInfo("exec",        (preInvoke)nativePreExec,        (invoke)nativeExec,        6, 2);
    addFunctionInfo("fail",        null,                            (invoke)nativeFail,        1, 0);
    addFunctionInfo("file",        null,                            (invoke)nativeFile,        3, 1);
    addFunctionInfo("filename",    null,                            (invoke)nativeFilename,    1, 1);
    addFunctionInfo("fileset",     null,                            (invoke)nativeFileset,     1, 1);
    addFunctionInfo("getCache",    null,                            (invoke)nativeGetCache,    2, 2);
    addFunctionInfo("getenv",      null,                            (invoke)nativeGetenv,      1, 1);
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
    objectref *p;
    uint i;
    struct
    {
        Work work;
        objectref values[NATIVE_MAX_VALUES];
    } env;

    assert(info->parameterCount + info->returnValueCount <= NATIVE_MAX_VALUES);
    env.work.vm = vm;
    env.work.condition = vm->condition;
    env.work.accessedFiles = 0;
    env.work.modifiedFiles = 0;
    VMPopMany(vm, env.values, info->parameterCount);
    memset(env.values + info->parameterCount, 0,
           info->returnValueCount * sizeof(env.values[0]));
    for (i = info->parameterCount, p = env.values; i; i--, p++)
    {
        *p = HeapTryWait(vm, *p);
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

nativefunctionref NativeFindFunction(stringref name)
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

stringref NativeGetName(nativefunctionref function)
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
