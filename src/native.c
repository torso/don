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

typedef struct
{
    VM *vm;
} FunctionEnv;

typedef void (*nativeInvoke)(FunctionEnv*);

typedef struct
{
    stringref name;
    nativeInvoke function;
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
    FunctionEnv fenv;

    objectref src;
    objectref dst;
} CpEnv;

static void nativeCp(CpEnv *env)
{
    assert(HeapIsFile(env->src));
    assert(HeapIsFile(env->dst));
    FileCopy(HeapGetFile(env->src), HeapGetFile(env->dst));
}

typedef struct
{
    FunctionEnv fenv;

    objectref message;
    objectref prefix;
} EchoEnv;

static void nativeEcho(EchoEnv *env)
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
    FunctionEnv fenv;

    objectref command;
    objectref env;
    objectref failOnError;
    objectref echoOut;
    objectref echoErr;

    objectref output;
    objectref status;
} ExecEnv;

static void nativeExec(ExecEnv *env)
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
        TaskFailVM(env->fenv.vm);
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
    FunctionEnv fenv;

    objectref message;
} FailEnv;

static noreturn void nativeFail(FailEnv *env)
{
    LogPrintErrObjectAutoNewline(env->message);
    TaskFailVM(env->fenv.vm);
}

typedef struct
{
    FunctionEnv fenv;

    objectref path;
    objectref name;
    objectref extension;

    objectref result;
} FileEnv;

static void nativeFile(FileEnv *env)
{
    fileref file = HeapGetFileFromParts(env->path, env->name, env->extension);

    env->result = HeapCreateFile(file);
}

typedef struct
{
    FunctionEnv fenv;

    objectref path;

    objectref result;
} FilenameEnv;

static void nativeFilename(FilenameEnv *env)
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
    FunctionEnv fenv;

    objectref value;

    objectref result;
} FilesetEnv;

/* TODO: Remove duplicate files. */
static void nativeFileset(FilesetEnv *env)
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
    FunctionEnv fenv;

    objectref key;
    objectref echoCachedOutput;

    objectref cacheFile;
    objectref uptodate;
} GetCacheEnv;

static void nativeGetCache(GetCacheEnv *env)
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
    FunctionEnv fenv;

    objectref name;

    objectref result;
} GetenvEnv;

static void nativeGetenv(GetenvEnv *env)
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
    FunctionEnv fenv;

    objectref data;
    objectref element;

    objectref result;
} IndexOfEnv;

static void nativeIndexOf(IndexOfEnv *env)
{
    /* TODO: Support collections */
    assert(HeapIsString(env->data));
    assert(HeapIsString(env->element));
    env->result = HeapStringIndexOf(env->data, 0, env->element);
}

typedef struct
{
    FunctionEnv fenv;

    objectref value;
    objectref trimEmptyLastLine;

    objectref result;
} LinesEnv;

static void nativeLines(LinesEnv *env)
{
    objectref content;

    content = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(content));
    env->result = HeapSplit(content, HeapNewline, false,
                            HeapIsTrue(env->trimEmptyLastLine));
}

typedef struct
{
    FunctionEnv fenv;

    objectref src;
    objectref dst;
} MvEnv;

static void nativeMv(MvEnv *env)
{
    assert(HeapIsFile(env->src));
    assert(HeapIsFile(env->dst));
    FileRename(HeapGetFile(env->src), HeapGetFile(env->dst), true);
}

typedef struct
{
    FunctionEnv fenv;

    objectref file;

    objectref result;
} ReadFileEnv;

static void nativeReadFile(ReadFileEnv *env)
{
    env->result = readFile(env->file);
}

typedef struct
{
    FunctionEnv fenv;

    objectref data;
    objectref original;
    objectref replacement;

    objectref result;
    objectref count;
} ReplaceEnv;

static void nativeReplace(ReplaceEnv *env)
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
    FunctionEnv fenv;

    objectref file;
} RmEnv;

static void nativeRm(RmEnv *env)
{
    FileDelete(HeapGetAsFile(env->file));
}

typedef struct
{
    FunctionEnv fenv;

    objectref cacheFile;
    objectref out;
    objectref err;
    objectref accessedFiles;
} SetUptodateEnv;

static void nativeSetUptodate(SetUptodateEnv *env)
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
    FunctionEnv fenv;

    objectref value;

    objectref result;
} SizeEnv;

static void nativeSize(SizeEnv *env)
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
    FunctionEnv fenv;

    objectref value;
    objectref delimiter;
    objectref removeEmpty;

    objectref result;
} SplitEnv;

static void nativeSplit(SplitEnv *env)
{
    objectref data = HeapIsFile(env->value) ? readFile(env->value) : env->value;
    assert(HeapIsString(data));
    assert(HeapIsString(env->delimiter));
    env->result = HeapSplit(data, env->delimiter, HeapIsTrue(env->removeEmpty),
                            false);
}


static void addFunctionInfo(const char *name, nativeInvoke function,
                            uint parameterCount, uint returnValueCount)
{
    functionInfo[initFunctionIndex].name = StringPoolAdd(name);
    functionInfo[initFunctionIndex].function = function;
    functionInfo[initFunctionIndex].parameterCount = parameterCount;
    functionInfo[initFunctionIndex].returnValueCount = returnValueCount;
    initFunctionIndex++;
}

void NativeInit(void)
{
    addFunctionInfo("cp", (nativeInvoke)nativeCp, 2, 0);
    addFunctionInfo("echo", (nativeInvoke)nativeEcho, 2, 0);
    addFunctionInfo("exec", (nativeInvoke)nativeExec, 5, 2);
    addFunctionInfo("fail", (nativeInvoke)nativeFail, 1, 0);
    addFunctionInfo("file", (nativeInvoke)nativeFile, 3, 1);
    addFunctionInfo("filename", (nativeInvoke)nativeFilename, 1, 1);
    addFunctionInfo("fileset", (nativeInvoke)nativeFileset, 1, 1);
    addFunctionInfo("getCache", (nativeInvoke)nativeGetCache, 2, 2);
    addFunctionInfo("getenv", (nativeInvoke)nativeGetenv, 1, 1);
    addFunctionInfo("indexOf", (nativeInvoke)nativeIndexOf, 2, 1);
    addFunctionInfo("lines", (nativeInvoke)nativeLines, 2, 1);
    addFunctionInfo("mv", (nativeInvoke)nativeMv, 2, 0);
    addFunctionInfo("readFile", (nativeInvoke)nativeReadFile, 1, 1);
    addFunctionInfo("replace", (nativeInvoke)nativeReplace, 3, 2);
    addFunctionInfo("rm", (nativeInvoke)nativeRm, 1, 0);
    addFunctionInfo("setUptodate", (nativeInvoke)nativeSetUptodate, 4, 0);
    addFunctionInfo("size", (nativeInvoke)nativeSize, 1, 1);
    addFunctionInfo("split", (nativeInvoke)nativeSplit, 3, 1);
}

void NativeInvoke(VM *vm, nativefunctionref function)
{
    const FunctionInfo *info = getFunctionInfo(function);
    struct
    {
        FunctionEnv fenv;
        objectref values[MAX_ENV];
    } env;

    assert(info->parameterCount + info->returnValueCount <= MAX_ENV);
    env.fenv.vm = vm;
    VMPopMany(vm, env.values, info->parameterCount);
    memset(env.values + info->parameterCount, 0, info->returnValueCount);
    info->function(&env.fenv);
    VMPushMany(vm, env.values + info->parameterCount, info->returnValueCount);
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
