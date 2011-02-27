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

#define NATIVE_FUNCTION_COUNT 19

typedef void (*nativeInvoke)(VM*);

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

static void nativeCp(VM *vm)
{
    objectref dst = InterpreterPop(vm);
    objectref src = InterpreterPop(vm);

    assert(HeapIsFile(src));
    assert(HeapIsFile(dst));
    FileCopy(HeapGetFile(src), HeapGetFile(dst));
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
        length = HeapStringLength(prefix);
        buffer = (char*)malloc(length);
        HeapWriteString(prefix, buffer);
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
    objectref output[2];
    const byte *p;
    size_t length;

    assert(HeapCollectionSize(env) % 2 == 0);

    argv = createStringArray(command);

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

        HeapIteratorInit(&iter, env, true);
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
    output[0] = HeapCreateString((const char*)p, length);
    LogPopOutBuffer();
    LogGetErrBuffer(&p, &length);
    output[1] = HeapCreateString((const char*)p, length);
    LogPopErrBuffer();
    LogAutoNewline();
    LogErrAutoNewline();
    InterpreterPush(vm, HeapCreateArray(output, 2));
    InterpreterPush(vm, HeapBoxInteger(status));
}

static noreturn void nativeFail(VM *vm)
{
    objectref message = InterpreterPop(vm);

    LogPrintErrObjectAutoNewline(message);
    TaskFailVM(vm);
}

static void nativeFile(VM *vm)
{
    objectref extension = InterpreterPop(vm);
    objectref name = InterpreterPop(vm);
    objectref path = InterpreterPop(vm);
    fileref file = HeapGetFileFromParts(path, name, extension);

    InterpreterPush(vm, HeapCreateFile(file));
}

static void nativeFilename(VM *vm)
{
    objectref path = InterpreterPop(vm);
    fileref file;
    size_t size;
    const char *text;

    assert(HeapIsFile(path));
    file = HeapGetFile(path);
    size = FileGetNameLength(file);
    text = FileFilename(FileGetName(file), &size);
    InterpreterPush(vm, HeapCreateString(text, size));
}

/* TODO: Remove duplicate files. */
static void nativeFileset(VM *vm)
{
    objectref value = InterpreterPop(vm);
    objectref o;
    intvector files;
    Iterator iter;

    IntVectorInit(&files);
    HeapIteratorInit(&iter, value, true);
    while (HeapIteratorNext(&iter, &o))
    {
        if (!HeapIsFile(o))
        {
            o = HeapCreateFile(HeapGetAsFile(o));
        }
        IntVectorAddRef(&files, o);
    }
    /* TODO: Reuse collection if possible. */
    InterpreterPush(vm, HeapCreateArrayFromVector(&files));
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
    HeapHash(key, &hashState);
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
    InterpreterPush(vm, HeapCreateFile(CacheGetFile(ref)));
    InterpreterPushBoolean(vm, uptodate);
}

static void nativeGetenv(VM *vm)
{
    objectref name = InterpreterPop(vm);
    char *buffer;
    size_t nameLength = HeapStringLength(name);
    const char *value;

    buffer = (char*)malloc(nameLength + 1);
    *HeapWriteString(name, buffer) = 0;
    value = getenv(buffer);
    free(buffer);
    InterpreterPush(vm, value ? HeapCreateString(value, strlen(value)) : 0);
}

static void nativeIndexOf(VM *vm)
{
    objectref element = InterpreterPop(vm);
    objectref data = InterpreterPop(vm);

    /* TODO: Support collections */
    assert(HeapIsString(data));
    assert(HeapIsString(element));
    InterpreterPush(vm, HeapStringIndexOf(data, 0, element));
}

static void nativeLines(VM *vm)
{
    boolean trimEmptyLastLine = InterpreterPopBoolean(vm);
    objectref value = InterpreterPop(vm);

    if (HeapIsFile(value))
    {
        value = readFile(value);
    }
    assert(HeapIsString(value));
    InterpreterPush(vm, HeapSplit(value, HeapNewline, false,
                                  trimEmptyLastLine));
}

static void nativeMv(VM *vm)
{
    objectref dst = InterpreterPop(vm);
    objectref src = InterpreterPop(vm);

    assert(HeapIsFile(src));
    assert(HeapIsFile(dst));
    FileRename(HeapGetFile(src), HeapGetFile(dst), true);
}

static void nativeReadFile(VM *vm)
{
    objectref file = InterpreterPop(vm);
    InterpreterPush(vm, readFile(file));
}

static void nativeReplace(VM *vm)
{
    objectref replacement = InterpreterPop(vm);
    objectref original = InterpreterPop(vm);
    objectref data = InterpreterPop(vm);
    size_t dataLength = HeapStringLength(data);
    size_t originalLength = HeapStringLength(original);
    size_t replacementLength = HeapStringLength(replacement);
    size_t offset;
    size_t newOffset;
    objectref offsetRef;
    char *p;
    uint replacements = 0;

    if (originalLength)
    {
        for (offset = 0;; offset++)
        {
            offsetRef = HeapStringIndexOf(data, offset, original);
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
        InterpreterPush(vm, data);
        InterpreterPush(vm, HeapBoxInteger(0));
        return;
    }
    InterpreterPush(
        vm, HeapCreateUninitialisedString(
            dataLength + replacements * (replacementLength - originalLength),
            &p));
    InterpreterPush(vm, HeapBoxUint(replacements));
    offset = 0;
    while (replacements--)
    {
        newOffset = HeapUnboxSize(HeapStringIndexOf(data, offset, original));
        p = HeapWriteSubstring(data, offset, newOffset - offset, p);
        p = HeapWriteString(replacement, p);
        offset = newOffset + originalLength;
    }
    HeapWriteSubstring(data, offset, dataLength - offset, p);
}

static void nativeRm(VM *vm)
{
    objectref file = InterpreterPop(vm);
    FileDelete(HeapGetAsFile(file));
}

static void nativeSetUptodate(VM *vm)
{
    objectref accessedFiles = InterpreterPop(vm);
    objectref err = InterpreterPop(vm);
    objectref out = InterpreterPop(vm);
    cacheref ref = CacheGetFromFile(HeapGetFile(InterpreterPop(vm)));
    objectref value;
    Iterator iter;
    size_t outLength = HeapStringLength(out);
    size_t errLength = HeapStringLength(err);
    char *output = null;

    if (accessedFiles)
    {
        HeapIteratorInit(&iter, accessedFiles, true);
        while (HeapIteratorNext(&iter, &value))
        {
            CacheAddDependency(ref, HeapGetFile(value));
        }
    }
    if (outLength || errLength)
    {
        output = (char*)malloc(outLength + errLength);
        HeapWriteString(out, output);
        HeapWriteString(err, output + outLength);
    }
    CacheSetUptodate(ref, outLength, errLength, output);
}

static void nativeSize(VM *vm)
{
    objectref value = InterpreterPop(vm);

    if (HeapIsCollection(value))
    {
        assert(HeapCollectionSize(value) <= INT_MAX);
        InterpreterPush(vm, HeapBoxSize(HeapCollectionSize(value)));
    }
    else
    {
        assert(HeapIsString(value));
        InterpreterPush(vm, HeapBoxSize(HeapStringLength(value)));
    }
}

static void nativeSplit(VM *vm)
{
    boolean removeEmpty = InterpreterPopBoolean(vm);
    objectref delimiter = InterpreterPop(vm);
    objectref value = InterpreterPop(vm);

    assert(HeapIsString(delimiter));
    if (HeapIsFile(value))
    {
        value = readFile(value);
    }
    assert(HeapIsString(value));
    InterpreterPush(
        vm, HeapSplit(value, delimiter, removeEmpty, false));
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
    nativeCp,
    nativeEcho,
    nativeExec,
    nativeFail,
    nativeFile,
    nativeFilename,
    nativeFileset,
    nativeGetCache,
    nativeGetenv,
    nativeIndexOf,
    nativeLines,
    nativeMv,
    nativeReadFile,
    nativeReplace,
    nativeRm,
    nativeSetUptodate,
    nativeSize,
    nativeSplit
};
