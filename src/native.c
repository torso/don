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
#include "native.h"
#include "stringpool.h"
#include "task.h"

#define TOTAL_PARAMETER_COUNT 33

typedef void (*nativeInvoke)(VM *, uint);

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
    uint vararg;
    ParameterInfo parameterInfo[1];
} FunctionInfo;

static byte functionInfo[
    (sizeof(FunctionInfo) -
     sizeof(ParameterInfo)) * (NATIVE_FUNCTION_COUNT - 1) +
    sizeof(ParameterInfo) * TOTAL_PARAMETER_COUNT];
static uint functionIndex[NATIVE_FUNCTION_COUNT];
static const nativeInvoke invokeTable[NATIVE_FUNCTION_COUNT];

static byte *initFunctionInfo = functionInfo;
static FunctionInfo *currentFunctionInfo;
static uint initFunctionIndex = 1;


static const FunctionInfo *getFunctionInfo(nativefunctionref function)
{
    assert(function);
    assert(uintFromRef(function) < NATIVE_FUNCTION_COUNT);
    return (FunctionInfo*)&functionInfo[functionIndex[sizeFromRef(function)]];
}

static fieldref addValue(bytevector *bytecode, Instruction op)
{
    size_t start = ByteVectorSize(bytecode);
    fieldref field = FieldIndexAdd(0, 0, 0);

    assert(field);
    ByteVectorAdd(bytecode, op);
    FieldIndexSetBytecodeOffset(field, start, ByteVectorSize(bytecode));
    return field;
}

static fieldref addStringValue(bytevector *bytecode, const char *string)
{
    size_t start = ByteVectorSize(bytecode);
    fieldref field = FieldIndexAdd(0, 0, 0);

    assert(field);
    ByteVectorAdd(bytecode, OP_STRING);
    ByteVectorAddRef(bytecode, StringPoolAdd(string));
    FieldIndexSetBytecodeOffset(field, start, ByteVectorSize(bytecode));
    return field;
}

static void addFunctionInfo(const char *name)
{
    functionIndex[initFunctionIndex++] =
        (uint)(initFunctionInfo - functionInfo);
    currentFunctionInfo = (FunctionInfo*)initFunctionInfo;
    initFunctionInfo += sizeof(FunctionInfo) - sizeof(ParameterInfo);

    currentFunctionInfo->name = StringPoolAdd(name);
    currentFunctionInfo->parameterCount = 0;
}

static void addParameter(const char *name, fieldref value, boolean vararg)
{
    ParameterInfo *info = (ParameterInfo*)initFunctionInfo;

    assert(!vararg || !value);
    info->name = StringPoolAdd(name);
    info->value = value;
    currentFunctionInfo->parameterCount++;
    if (vararg)
    {
        assert(!currentFunctionInfo->vararg);
        currentFunctionInfo->vararg = currentFunctionInfo->parameterCount;
    }
    initFunctionInfo += sizeof(ParameterInfo);
}

void NativeInit(bytevector *bytecode)
{
    fieldref valueNull = addValue(bytecode, OP_NULL);
    fieldref valueFalse = addValue(bytecode, OP_FALSE);
    fieldref valueTrue = addValue(bytecode, OP_TRUE);
    fieldref valueSpace = addStringValue(bytecode, " ");

    addFunctionInfo("echo");
    addParameter("message", 0, false);
    addParameter("prefix", valueNull, false);

    addFunctionInfo("exec");
    addParameter("command", 0, true);
    addParameter("failOnError", valueTrue, false);
    addParameter("echo", valueTrue, false);
    addParameter("echoStderr", valueTrue, false);

    addFunctionInfo("fail");
    addParameter("message", valueNull, false);
    addParameter("condition", valueTrue, false);

    addFunctionInfo("file");
    addParameter("path", 0, false);
    addParameter("name", 0, false);
    addParameter("extension", valueNull, false);

    addFunctionInfo("filename");
    addParameter("path", 0, false);

    addFunctionInfo("fileset");
    addParameter("value", 0, false);

    addFunctionInfo("getCache");
    addParameter("label", 0, false);
    addParameter("version", 0, false);
    addParameter("key", 0, true);
    addParameter("echoCachedOutput", valueTrue, false);

    addFunctionInfo("indexOf");
    addParameter("data", 0, false);
    addParameter("element", 0, false);

    addFunctionInfo("lines");
    addParameter("value", 0, false);
    addParameter("trimEmptyLastLine", valueTrue, false);

    addFunctionInfo("readFile");
    addParameter("file", 0, false);

    addFunctionInfo("replace");
    addParameter("data", 0, false);
    addParameter("original", 0, false);
    addParameter("replacement", 0, false);

    addFunctionInfo("setUptodate");
    addParameter("cacheFile", 0, false);
    addParameter("out", valueNull, false);
    addParameter("err", valueNull, false);
    addParameter("accessedFiles", valueNull, false);

    addFunctionInfo("size");
    addParameter("value", 0, false);

    addFunctionInfo("split");
    addParameter("value", 0, false);
    addParameter("delimiter", valueSpace, false);
    addParameter("removeEmpty", valueFalse, false);

    assert(initFunctionInfo == functionInfo + sizeof(functionInfo));
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

static void nativeEcho(VM *vm, uint returnValues)
{
    objectref prefix = InterpreterPop(vm);
    objectref message = InterpreterPop(vm);
    char *buffer;
    size_t length;

    assert(!returnValues);
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

static void nativeExec(VM *vm, uint returnValues)
{
    boolean echoErr = InterpreterPopBoolean(vm);
    boolean echoOut = InterpreterPopBoolean(vm);
    boolean failOnError = InterpreterPopBoolean(vm);
    objectref command = InterpreterPop(vm);
    char **argv;
    pid_t pid;
    int status;
    int pipeOut[2];
    int pipeErr[2];
    objectref log;
    const byte *p;
    size_t length;

    assert(returnValues <= 3);
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

    if (returnValues)
    {
        LogPushOutBuffer(echoOut);
    }
    if (returnValues >= 3)
    {
        LogPushErrBuffer(echoErr);
    }
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
    if (returnValues)
    {
        LogGetOutBuffer(&p, &length);
        log = HeapCreateString(vm, (const char*)p, length);
        LogPopOutBuffer();
        InterpreterPush(vm, log);
    }
    if (returnValues >= 2)
    {
        InterpreterPush(vm, HeapBoxInteger(vm, status));
    }
    if (returnValues >= 3)
    {
        LogGetErrBuffer(&p, &length);
        log = HeapCreateString(vm, (const char*)p, length);
        LogPopErrBuffer();
        InterpreterPush(vm, log);
    }
    LogAutoNewline();
}

static void nativeFail(VM *vm, uint returnValues)
{
    boolean condition = InterpreterPopBoolean(vm);
    objectref message = InterpreterPop(vm);

    assert(!returnValues);
    if (!condition)
    {
        return;
    }
    LogPrintErrSZ("BUILD FAILED");
    if (!message || !HeapStringLength(vm, message))
    {
        LogErrNewline();
    }
    else
    {
        LogPrintErrSZ(": ");
        LogPrintErrObjectAutoNewline(vm, message);
    }
    TaskFailVM(vm);
}

static void nativeFile(VM *vm, uint returnValues)
{
    objectref extension = InterpreterPop(vm);
    objectref name = InterpreterPop(vm);
    objectref path = InterpreterPop(vm);
    fileref file = HeapGetFileFromParts(vm, path, name, extension);

    assert(returnValues <= 1);
    if (returnValues)
    {
        InterpreterPush(vm, HeapCreateFile(vm, file));
    }
}

static void nativeFilename(VM *vm, uint returnValues)
{
    objectref path = InterpreterPop(vm);
    fileref file;
    size_t size;
    const char *text;

    assert(returnValues <= 1);
    assert(HeapIsFile(vm, path));
    if (returnValues)
    {
        file = HeapGetFile(vm, path);
        size = FileGetNameLength(file);
        text = FileFilename(FileGetName(file), &size);
        InterpreterPush(vm, HeapCreateString(vm, text, size));
    }
}

/* TODO: Remove duplicate files. */
static void nativeFileset(VM *vm, uint returnValues)
{
    objectref value = InterpreterPop(vm);
    objectref o;
    intvector files;
    Iterator iter;

    assert(returnValues <= 1);
    if (returnValues)
    {
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
}

static void nativeGetCache(VM *vm, uint returnValues)
{
    boolean echoCachedOutput = InterpreterPopBoolean(vm);
    objectref key = InterpreterPop(vm);
    objectref version = InterpreterPop(vm);
    objectref label = InterpreterPop(vm);
    cacheref ref;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    boolean uptodate;

    assert(returnValues <= 2);
    HashInit(&hashState);
    HeapHash(vm, label, &hashState);
    HeapHash(vm, version, &hashState);
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
    if (returnValues)
    {
        InterpreterPush(vm, HeapCreateFile(vm, CacheGetFile(ref)));
    }
    if (returnValues > 1)
    {
        InterpreterPushBoolean(vm, uptodate);
    }
}

static void nativeIndexOf(VM *vm, uint returnValues)
{
    objectref element = InterpreterPop(vm);
    objectref data = InterpreterPop(vm);

    /* TODO: Support collections */
    assert(returnValues <= 1);
    assert(HeapIsString(vm, data));
    assert(HeapIsString(vm, element));
    if (returnValues)
    {
        InterpreterPush(vm, HeapStringIndexOf(vm, data, 0, element));
    }
}

static void nativeLines(VM *vm, uint returnValues)
{
    boolean trimEmptyLastLine = InterpreterPopBoolean(vm);
    objectref value = InterpreterPop(vm);

    assert(returnValues <= 1);
    if (returnValues)
    {
        if (HeapIsFile(vm, value))
        {
            value = readFile(vm, value);
        }
        assert(HeapIsString(vm, value));
        InterpreterPush(vm, HeapSplit(vm, value, vm->stringNewline, false,
                                      trimEmptyLastLine));
    }
}

static void nativeReadFile(VM *vm, uint returnValues)
{
    objectref file = InterpreterPop(vm);

    assert(returnValues <= 1);
    if (returnValues)
    {
        InterpreterPush(vm, readFile(vm, file));
    }
}

static void nativeReplace(VM *vm, uint returnValues)
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

    assert(returnValues <= 2);
    if (!returnValues)
    {
        return;
    }

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
        if (returnValues > 1)
        {
            InterpreterPush(vm, HeapBoxInteger(vm, 0));
        }
        return;
    }
    InterpreterPush(
        vm, HeapCreateUninitialisedString(
            vm,
            dataLength + replacements * (replacementLength - originalLength),
            &p));
    if (returnValues > 1)
    {
        InterpreterPush(vm, HeapBoxUint(vm, replacements));
    }
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

static void nativeSetUptodate(VM *vm, uint returnValues)
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

    assert(!returnValues);
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

static void nativeSize(VM *vm, uint returnValues)
{
    objectref value = InterpreterPop(vm);

    assert(returnValues <= 1);
    if (returnValues)
    {
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
}

static void nativeSplit(VM *vm, uint returnValues)
{
    boolean removeEmpty = InterpreterPopBoolean(vm);
    objectref delimiter = InterpreterPop(vm);
    objectref value = InterpreterPop(vm);

    assert(HeapIsString(vm, delimiter));
    assert(returnValues <= 1);
    if (returnValues)
    {
        if (HeapIsFile(vm, value))
        {
            value = readFile(vm, value);
        }
        assert(HeapIsString(vm, value));
        InterpreterPush(
            vm, HeapSplit(vm, value, delimiter, removeEmpty, false));
    }
}

void NativeInvoke(VM *vm, nativefunctionref function, uint returnValues)
{
    invokeTable[function](vm, returnValues);
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

const ParameterInfo *NativeGetParameterInfo(nativefunctionref function)
{
    return getFunctionInfo(function)->parameterInfo;
}

boolean NativeHasVararg(nativefunctionref function)
{
    return getFunctionInfo(function)->vararg != 0;
}

uint NativeGetVarargIndex(nativefunctionref function)
{
    assert(NativeHasVararg(function));
    return getFunctionInfo(function)->vararg - 1;
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
