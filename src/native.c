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
#include "instruction.h"
#include "interpreter.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "stringpool.h"
#include "task.h"

typedef void (*nativeInvoke)(VM*);

typedef enum
{
    NATIVE_NULL,
    NATIVE_ECHO,
    NATIVE_EXEC,
    NATIVE_FAIL,
    NATIVE_FILE,
    NATIVE_FILENAME,
    NATIVE_FILESET,
    NATIVE_GETCACHE,
    NATIVE_INDEXOF,
    NATIVE_LINES,
    NATIVE_READFILE,
    NATIVE_REPLACE,
    NATIVE_SETUPTODATE,
    NATIVE_SIZE,
    NATIVE_SPLIT,

    NATIVE_FUNCTION_COUNT
} NativeFunction;

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
    addFunctionInfo("echo", 2, 0);
    addFunctionInfo("exec", 5, 3);
    addFunctionInfo("fail", 1, 0);
    addFunctionInfo("file", 3, 1);
    addFunctionInfo("filename", 1, 1);
    addFunctionInfo("fileset", 1, 1);
    addFunctionInfo("getCache", 2, 2);
    addFunctionInfo("indexOf", 2, 1);
    addFunctionInfo("lines", 2, 1);
    addFunctionInfo("readFile", 1, 1);
    addFunctionInfo("replace", 3, 2);
    addFunctionInfo("setUptodate", 4, 0);
    addFunctionInfo("size", 1, 1);
    addFunctionInfo("split", 3, 1);
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

    assert(HeapIsCollection(vm, collection));
    assert(HeapCollectionSize(vm, collection));

    HeapIteratorInit(vm, &iter, collection, true);
    while (HeapIteratorNext(&iter, &value))
    {
        size += HeapStringLength(vm, value) + 1 + sizeof(char*);
        count++;
    }

    strings = (char**)malloc(size);

    table = strings;
    stringData = (char*)&strings[count];
    HeapIteratorInit(vm, &iter, collection, true);
    while (HeapIteratorNext(&iter, &value))
    {
        *table++ = stringData;
        stringData = HeapWriteString(vm, value, stringData);
        *stringData++ = 0;
    }
    *table = null;
    return strings;
}

static objectref readFile(VM *vm, objectref object)
{
    const char *text;
    size_t size;
    fileref file = HeapGetFile(vm, object);

    FileMMap(file, (const byte**)&text, &size, true);
    return HeapCreateWrappedString(vm, text, size);
}

static void nativeEcho(VM *vm)
{
    objectref prefix = InterpreterPop(vm);
    objectref message = InterpreterPop(vm);
    char *buffer;
    size_t length;

    if (prefix)
    {
        /* TODO: Avoid malloc */
        length = HeapStringLength(vm, prefix);
        buffer = (char*)malloc(length);
        HeapWriteString(vm, prefix, buffer);
        LogSetPrefix(buffer, length);
        LogPrintObjectAutoNewline(vm, message);
        LogSetPrefix(null, 0);
        free(buffer);
    }
    else
    {
        LogPrintObjectAutoNewline(vm, message);
    }
}

static void nativeExec(VM *vm)
{
    boolean echoErr = InterpreterPopBoolean(vm);
    boolean echoOut = InterpreterPopBoolean(vm);
    boolean failOnError = InterpreterPopBoolean(vm);
    objectref env = InterpreterPop(vm);
    objectref command = InterpreterPop(vm);
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
    objectref log;
    const byte *p;
    size_t length;

    assert(HeapCollectionSize(vm, env) % 2 == 0);

    argv = createStringArray(vm, command);

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

        HeapIteratorInit(vm, &iter, env, true);
        while (HeapIteratorNext(&iter, &name))
        {
            HeapIteratorNext(&iter, &value);
            if (value)
            {
                pname = (char*)malloc(HeapStringLength(vm, name) +
                                      HeapStringLength(vm, value) + 2);
                pvalue = HeapWriteString(vm, name, pname);
                *pvalue++ = 0;
                *HeapWriteString(vm, value, pvalue) = 0;
                setenv(pname, pvalue, 1);
                free(pname);
            }
            else
            {
                pname = (char*)malloc(HeapStringLength(vm, name) + 1);
                *HeapWriteString(vm, name, pname) = 0;
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

    LogPushOutBuffer(echoOut);
    LogPushErrBuffer(echoErr);
    LogConsumePipes(pipeOut[0], pipeErr[0]);

    pid = waitpid(pid, &status, 0);
    if (pid < 0)
    {
        TaskFailErrno(false);
    }
    if (failOnError && status)
    {
        fprintf(stderr, "BUILD ERROR: Process exited with status %d.\n", status);
        TaskFailVM(vm);
    }
    LogGetOutBuffer(&p, &length);
    log = HeapCreateString(vm, (const char*)p, length);
    LogPopOutBuffer();
    InterpreterPush(vm, log);
    InterpreterPush(vm, HeapBoxInteger(vm, status));
    LogGetErrBuffer(&p, &length);
    log = HeapCreateString(vm, (const char*)p, length);
    LogPopErrBuffer();
    InterpreterPush(vm, log);
    LogAutoNewline();
    LogErrAutoNewline();
}

static noreturn void nativeFail(VM *vm)
{
    objectref message = InterpreterPop(vm);

    LogPrintErrObjectAutoNewline(vm, message);
    TaskFailVM(vm);
}

static void nativeFile(VM *vm)
{
    objectref extension = InterpreterPop(vm);
    objectref name = InterpreterPop(vm);
    objectref path = InterpreterPop(vm);
    fileref file = HeapGetFileFromParts(vm, path, name, extension);

    InterpreterPush(vm, HeapCreateFile(vm, file));
}

static void nativeFilename(VM *vm)
{
    objectref path = InterpreterPop(vm);
    fileref file;
    size_t size;
    const char *text;

    assert(HeapIsFile(vm, path));
    file = HeapGetFile(vm, path);
    size = FileGetNameLength(file);
    text = FileFilename(FileGetName(file), &size);
    InterpreterPush(vm, HeapCreateString(vm, text, size));
}

/* TODO: Remove duplicate files. */
static void nativeFileset(VM *vm)
{
    objectref value = InterpreterPop(vm);
    objectref o;
    intvector files;
    Iterator iter;

    IntVectorInit(&files);
    HeapIteratorInit(vm, &iter, value, true);
    while (HeapIteratorNext(&iter, &o))
    {
        if (!HeapIsFile(vm, o))
        {
            IntVectorAddRef(
                &files,
                HeapCreateFile(vm, HeapGetFileFromParts(vm, 0, o, 0)));
        }
        else
        {
            IntVectorAddRef(&files, o);
        }
    }
    /* TODO: Reuse collection if possible. */
    InterpreterPush(vm, HeapCreateArray(vm, &files));
    IntVectorDispose(&files);
}

static void nativeGetCache(VM *vm)
{
    boolean echoCachedOutput = InterpreterPopBoolean(vm);
    objectref key = InterpreterPop(vm);
    cacheref ref;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    boolean uptodate;

    HashInit(&hashState);
    HeapHash(vm, key, &hashState);
    HashFinal(&hashState, hash);
    ref = CacheGet(hash);
    uptodate = CacheCheckUptodate(ref);
    if (uptodate)
    {
        if (echoCachedOutput)
        {
            CacheEchoCachedOutput(ref);
        }
    }
    else
    {
        FileMkdir(CacheGetFile(ref));
    }
    InterpreterPush(vm, HeapCreateFile(vm, CacheGetFile(ref)));
    InterpreterPushBoolean(vm, uptodate);
}

static void nativeIndexOf(VM *vm)
{
    objectref element = InterpreterPop(vm);
    objectref data = InterpreterPop(vm);

    /* TODO: Support collections */
    assert(HeapIsString(vm, data));
    assert(HeapIsString(vm, element));
    InterpreterPush(vm, HeapStringIndexOf(vm, data, 0, element));
}

static void nativeLines(VM *vm)
{
    boolean trimEmptyLastLine = InterpreterPopBoolean(vm);
    objectref value = InterpreterPop(vm);

    if (HeapIsFile(vm, value))
    {
        value = readFile(vm, value);
    }
    assert(HeapIsString(vm, value));
    InterpreterPush(vm, HeapSplit(vm, value, vm->stringNewline, false,
                                  trimEmptyLastLine));
}

static void nativeReadFile(VM *vm)
{
    objectref file = InterpreterPop(vm);
    InterpreterPush(vm, readFile(vm, file));
}

static void nativeReplace(VM *vm)
{
    objectref replacement = InterpreterPop(vm);
    objectref original = InterpreterPop(vm);
    objectref data = InterpreterPop(vm);
    size_t dataLength = HeapStringLength(vm, data);
    size_t originalLength = HeapStringLength(vm, original);
    size_t replacementLength = HeapStringLength(vm, replacement);
    size_t offset;
    size_t newOffset;
    objectref offsetRef;
    char *p;
    uint replacements = 0;

    if (originalLength)
    {
        for (offset = 0;; offset++)
        {
            offsetRef = HeapStringIndexOf(vm, data, offset, original);
            if (!offsetRef)
            {
                break;
            }
            replacements++;
            offset = HeapUnboxSize(vm, offsetRef);
        }
    }
    if (!replacements)
    {
        InterpreterPush(vm, data);
        InterpreterPush(vm, HeapBoxInteger(vm, 0));
        return;
    }
    InterpreterPush(
        vm, HeapCreateUninitialisedString(
            vm,
            dataLength + replacements * (replacementLength - originalLength),
            &p));
    InterpreterPush(vm, HeapBoxUint(vm, replacements));
    offset = 0;
    while (replacements--)
    {
        newOffset = HeapUnboxSize(vm, HeapStringIndexOf(vm, data,
                                                        offset, original));
        p = HeapWriteSubstring(vm, data, offset, newOffset - offset, p);
        p = HeapWriteString(vm, replacement, p);
        offset = newOffset + originalLength;
    }
    HeapWriteSubstring(vm, data, offset, dataLength - offset, p);
}

static void nativeSetUptodate(VM *vm)
{
    objectref accessedFiles = InterpreterPop(vm);
    objectref err = InterpreterPop(vm);
    objectref out = InterpreterPop(vm);
    cacheref ref = CacheGetFromFile(HeapGetFile(vm, InterpreterPop(vm)));
    objectref value;
    Iterator iter;
    size_t outLength = HeapStringLength(vm, out);
    size_t errLength = HeapStringLength(vm, err);
    char *output = null;

    if (accessedFiles)
    {
        HeapIteratorInit(vm, &iter, accessedFiles, true);
        while (HeapIteratorNext(&iter, &value))
        {
            CacheAddDependency(ref, HeapGetFile(vm, value));
        }
    }
    if (outLength || errLength)
    {
        output = (char*)malloc(outLength + errLength);
        HeapWriteString(vm, out, output);
        HeapWriteString(vm, err, output + outLength);
    }
    CacheSetUptodate(ref, outLength, errLength, output);
}

static void nativeSize(VM *vm)
{
    objectref value = InterpreterPop(vm);

    if (HeapIsCollection(vm, value))
    {
        assert(HeapCollectionSize(vm, value) <= INT_MAX);
        InterpreterPush(vm, HeapBoxSize(vm, HeapCollectionSize(vm, value)));
    }
    else
    {
        assert(HeapIsString(vm, value));
        InterpreterPush(vm, HeapBoxSize(vm, HeapStringLength(vm, value)));
    }
}

static void nativeSplit(VM *vm)
{
    boolean removeEmpty = InterpreterPopBoolean(vm);
    objectref delimiter = InterpreterPop(vm);
    objectref value = InterpreterPop(vm);

    assert(HeapIsString(vm, delimiter));
    if (HeapIsFile(vm, value))
    {
        value = readFile(vm, value);
    }
    assert(HeapIsString(vm, value));
    InterpreterPush(
        vm, HeapSplit(vm, value, delimiter, removeEmpty, false));
}

void NativeInvoke(VM *vm, nativefunctionref function)
{
    invokeTable[function](vm);
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
    nativeEcho,
    nativeExec,
    nativeFail,
    nativeFile,
    nativeFilename,
    nativeFileset,
    nativeGetCache,
    nativeIndexOf,
    nativeLines,
    nativeReadFile,
    nativeReplace,
    nativeSetUptodate,
    nativeSize,
    nativeSplit
};
