#define _XOPEN_SOURCE 600
#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "cache.h"
#include "fieldindex.h"
#include "file.h"
#include "hash.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "stringpool.h"
#include "task.h"

#define NATIVE_FUNCTION_COUNT 19
#define MAX_ENV 7

typedef void (*nativeInvoke)(VM*, objectref*);

typedef struct
{
    stringref name;
    uint parameterCount;
    uint returnValueCount;
} FunctionInfo;

static FunctionInfo functionInfo[NATIVE_FUNCTION_COUNT];
static const nativeInvoke invokeTable[NATIVE_FUNCTION_COUNT];
static uint initFunctionIndex = 1;


static const FunctionInfo *getFunctionInfo(nativefunctionref function)
{
    assert(function);
    assert(uintFromRef(function) < NATIVE_FUNCTION_COUNT);
    return (FunctionInfo*)&functionInfo[sizeFromRef(function)];
}

static void addFunctionInfo(const char *name, uint parameterCount,
                            uint returnValueCount)
{
    functionInfo[initFunctionIndex].name = StringPoolAdd(name);
    functionInfo[initFunctionIndex].parameterCount = parameterCount;
    functionInfo[initFunctionIndex].returnValueCount = returnValueCount;
    initFunctionIndex++;
}

void NativeInit(void)
{
    addFunctionInfo("cp", 2, 0);
    addFunctionInfo("echo", 2, 0);
    addFunctionInfo("exec", 5, 2);
    addFunctionInfo("fail", 1, 0);
    addFunctionInfo("file", 3, 1);
    addFunctionInfo("filename", 1, 1);
    addFunctionInfo("fileset", 1, 1);
    addFunctionInfo("getCache", 2, 2);
    addFunctionInfo("getenv", 1, 1);
    addFunctionInfo("indexOf", 2, 1);
    addFunctionInfo("lines", 2, 1);
    addFunctionInfo("mv", 2, 0);
    addFunctionInfo("readFile", 1, 1);
    addFunctionInfo("replace", 3, 2);
    addFunctionInfo("rm", 1, 0);
    addFunctionInfo("setUptodate", 4, 0);
    addFunctionInfo("size", 1, 1);
    addFunctionInfo("split", 3, 1);
}

static char **createStringArray(objectref collection)
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
    while (HeapIteratorNext(&iter, &value))
    {
        size += HeapStringLength(value) + 1 + sizeof(char*);
        count++;
    }

    strings = (char**)malloc(size);

    table = strings;
    stringData = (char*)&strings[count];
    HeapIteratorInit(&iter, collection, true);
    while (HeapIteratorNext(&iter, &value))
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
    const char *text;
    size_t size;
    fileref file = HeapGetAsFile(object);

    FileMMap(file, (const byte**)&text, &size, true);
    return HeapCreateWrappedString(text, size);
}

typedef struct
{
    objectref src;
    objectref dst;
} CpEnv;

static void nativeCp(VM *vm unused, CpEnv *env)
{
    assert(HeapIsFile(env->src));
    assert(HeapIsFile(env->dst));
    FileCopy(HeapGetFile(env->src), HeapGetFile(env->dst));
}

typedef struct
{
    objectref message;
    objectref prefix;
} EchoEnv;

static void nativeEcho(VM *vm unused, EchoEnv *env)
{
    char *buffer;
    size_t length;

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
}

typedef struct
{
    objectref command;
    objectref env;
    objectref failOnError;
    objectref echoOut;
    objectref echoErr;

    objectref output;
    objectref status;
} ExecEnv;

static void nativeExec(VM *vm, ExecEnv *env)
{
    Iterator iter;
    objectref name;
    objectref value;
    char *pname;
    char *pvalue;
    char **argv;
    pid_t pid;
    int status;
    int pipeOut[2];
    int pipeErr[2];
    objectref output[2];
    const byte *p;
    size_t length;

    assert(HeapCollectionSize(env->env) % 2 == 0);

    argv = createStringArray(env->command);

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

        HeapIteratorInit(&iter, env->env, true);
        while (HeapIteratorNext(&iter, &name))
        {
            HeapIteratorNext(&iter, &value);
            if (value)
            {
                pname = (char*)malloc(HeapStringLength(name) +
                                      HeapStringLength(value) + 2);
                pvalue = HeapWriteString(name, pname);
                *pvalue++ = 0;
                *HeapWriteString(value, pvalue) = 0;
                setenv(pname, pvalue, 1);
                free(pname);
            }
            else
            {
                pname = (char*)malloc(HeapStringLength(name) + 1);
                *HeapWriteString(name, pname) = 0;
                unsetenv(pname);
                free(pname);
            }
        }

        execvp(argv[0], argv);
        _exit(EXIT_FAILURE);
    }
    free(argv);
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
    if (HeapIsTrue(env->failOnError) && status)
    {
        fprintf(stderr, "BUILD ERROR: Process exited with status %d.\n", status);
        TaskFailVM(vm);
    }
    LogGetOutBuffer(&p, &length);
    output[0] = HeapCreateString((const char*)p, length);
    LogPopOutBuffer();
    LogGetErrBuffer(&p, &length);
    output[1] = HeapCreateString((const char*)p, length);
    LogPopErrBuffer();
    LogAutoNewline();
    LogErrAutoNewline();
    env->output = HeapCreateArray(output, 2);
    env->status = HeapBoxInteger(status);
}

typedef struct
{
    objectref message;
} FailEnv;

static noreturn void nativeFail(VM *vm, FailEnv *env)
{
    LogPrintErrObjectAutoNewline(env->message);
    TaskFailVM(vm);
}

typedef struct
{
    objectref path;
    objectref name;
    objectref extension;

    objectref result;
} FileEnv;

static void nativeFile(VM *vm unused, FileEnv *env)
{
    fileref file = HeapGetFileFromParts(env->path, env->name, env->extension);

    env->result = HeapCreateFile(file);
}

typedef struct
{
    objectref path;

    objectref result;
} FilenameEnv;

static void nativeFilename(VM *vm unused, FilenameEnv *env)
{
    fileref file;
    size_t size;
    const char *text;

    assert(HeapIsFile(env->path));
    file = HeapGetFile(env->path);
    size = FileGetNameLength(file);
    text = FileFilename(FileGetName(file), &size);
    env->result = HeapCreateString(text, size);
}

typedef struct
{
    objectref value;

    objectref result;
} FilesetEnv;

/* TODO: Remove duplicate files. */
static void nativeFileset(VM *vm unused, FilesetEnv *env)
{
    objectref o;
    intvector files;
    Iterator iter;

    IntVectorInit(&files);
    HeapIteratorInit(&iter, env->value, true);
    while (HeapIteratorNext(&iter, &o))
    {
        if (!HeapIsFile(o))
        {
            o = HeapCreateFile(HeapGetAsFile(o));
        }
        IntVectorAddRef(&files, o);
    }
    /* TODO: Reuse collection if possible. */
    env->result = HeapCreateArrayFromVector(&files);
    IntVectorDispose(&files);
}

typedef struct
{
    objectref key;
    objectref echoCachedOutput;

    objectref cacheFile;
    objectref uptodate;
} GetCacheEnv;

static void nativeGetCache(VM *vm unused, GetCacheEnv *env)
{
    cacheref ref;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    boolean uptodate;

    HashInit(&hashState);
    HeapHash(env->key, &hashState);
    HashFinal(&hashState, hash);
    ref = CacheGet(hash);
    uptodate = CacheCheckUptodate(ref);
    if (uptodate)
    {
        if (HeapIsTrue(env->echoCachedOutput))
        {
            CacheEchoCachedOutput(ref);
        }
    }
    else
    {
        FileMkdir(CacheGetFile(ref));
    }
    env->cacheFile = HeapCreateFile(CacheGetFile(ref));
    env->uptodate = uptodate ? HeapTrue : HeapFalse;
}

typedef struct
{
    objectref name;

    objectref result;
} GetenvEnv;

static void nativeGetenv(VM *vm unused, GetenvEnv *env)
{
    char *buffer;
    size_t nameLength = HeapStringLength(env->name);
    const char *value;

    buffer = (char*)malloc(nameLength + 1);
    *HeapWriteString(env->name, buffer) = 0;
    value = getenv(buffer);
    free(buffer);
    env->result = value ? HeapCreateString(value, strlen(value)) : 0;
}

typedef struct
{
    objectref data;
    objectref element;

    objectref result;
} IndexOfEnv;

static void nativeIndexOf(VM *vm unused, IndexOfEnv *env)
{
    /* TODO: Support collections */
    assert(HeapIsString(env->data));
    assert(HeapIsString(env->element));
    env->result = HeapStringIndexOf(env->data, 0, env->element);
}

typedef struct
{
    objectref value;
    objectref trimEmptyLastLine;

    objectref result;
} LinesEnv;

static void nativeLines(VM *vm unused, LinesEnv *env)
{
    objectref content;

    content = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(content));
    env->result = HeapSplit(content, HeapNewline, false,
                            HeapIsTrue(env->trimEmptyLastLine));
}

typedef struct
{
    objectref src;
    objectref dst;
} MvEnv;

static void nativeMv(VM *vm unused, MvEnv *env)
{
    assert(HeapIsFile(env->src));
    assert(HeapIsFile(env->dst));
    FileRename(HeapGetFile(env->src), HeapGetFile(env->dst), true);
}

typedef struct
{
    objectref file;

    objectref result;
} ReadFileEnv;

static void nativeReadFile(VM *vm unused, ReadFileEnv *env)
{
    env->result = readFile(env->file);
}

typedef struct
{
    objectref data;
    objectref original;
    objectref replacement;

    objectref result;
    objectref count;
} ReplaceEnv;

static void nativeReplace(VM *vm unused, ReplaceEnv *env)
{
    size_t dataLength = HeapStringLength(env->data);
    size_t originalLength = HeapStringLength(env->original);
    size_t replacementLength = HeapStringLength(env->replacement);
    size_t offset;
    size_t newOffset;
    objectref offsetRef;
    char *p;
    uint replacements = 0;

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
        return;
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
}

typedef struct
{
    objectref file;
} RmEnv;

static void nativeRm(VM *vm unused, RmEnv *env)
{
    FileDelete(HeapGetAsFile(env->file));
}

typedef struct
{
    objectref cacheFile;
    objectref out;
    objectref err;
    objectref accessedFiles;
} SetUptodateEnv;

static void nativeSetUptodate(VM *vm unused, SetUptodateEnv *env)
{
    cacheref ref = CacheGetFromFile(HeapGetFile(env->cacheFile));
    objectref value;
    Iterator iter;
    size_t outLength = HeapStringLength(env->out);
    size_t errLength = HeapStringLength(env->err);
    char *output = null;

    if (env->accessedFiles)
    {
        HeapIteratorInit(&iter, env->accessedFiles, true);
        while (HeapIteratorNext(&iter, &value))
        {
            CacheAddDependency(ref, HeapGetFile(value));
        }
    }
    if (outLength || errLength)
    {
        output = (char*)malloc(outLength + errLength);
        HeapWriteString(env->out, output);
        HeapWriteString(env->err, output + outLength);
    }
    CacheSetUptodate(ref, outLength, errLength, output);
}

typedef struct
{
    objectref value;

    objectref result;
} SizeEnv;

static void nativeSize(VM *vm unused, SizeEnv *env)
{
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
}

typedef struct
{
    objectref value;
    objectref delimiter;
    objectref removeEmpty;

    objectref result;
} SplitEnv;

static void nativeSplit(VM *vm unused, SplitEnv *env)
{
    objectref data = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(data));
    assert(HeapIsString(env->delimiter));
    env->result = HeapSplit(data, env->delimiter, HeapIsTrue(env->removeEmpty),
                            false);
}

void NativeInvoke(VM *vm, nativefunctionref function)
{
    const FunctionInfo *info = getFunctionInfo(function);
    objectref env[MAX_ENV];

    assert(info->parameterCount + info->returnValueCount <= MAX_ENV);
    VMPopMany(vm, env, info->parameterCount);
    memset(env + info->parameterCount, 0, info->returnValueCount);
    invokeTable[function](vm, env);
    VMPushMany(vm, env + info->parameterCount, info->returnValueCount);
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

static const nativeInvoke invokeTable[NATIVE_FUNCTION_COUNT] =
{
    null,
    (nativeInvoke)nativeCp,
    (nativeInvoke)nativeEcho,
    (nativeInvoke)nativeExec,
    (nativeInvoke)nativeFail,
    (nativeInvoke)nativeFile,
    (nativeInvoke)nativeFilename,
    (nativeInvoke)nativeFileset,
    (nativeInvoke)nativeGetCache,
    (nativeInvoke)nativeGetenv,
    (nativeInvoke)nativeIndexOf,
    (nativeInvoke)nativeLines,
    (nativeInvoke)nativeMv,
    (nativeInvoke)nativeReadFile,
    (nativeInvoke)nativeReplace,
    (nativeInvoke)nativeRm,
    (nativeInvoke)nativeSetUptodate,
    (nativeInvoke)nativeSize,
    (nativeInvoke)nativeSplit
};
