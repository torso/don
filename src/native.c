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

#define TOTAL_PARAMETER_COUNT 18

typedef enum
{
    NATIVE_NULL,
    NATIVE_ECHO,
    NATIVE_EXEC,
    NATIVE_FAIL,
    NATIVE_FILENAME,
    NATIVE_GETCACHE,
    NATIVE_ISUPTODATE,
    NATIVE_LINES,
    NATIVE_READFILE,
    NATIVE_SETUPTODATE,
    NATIVE_SIZE,

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
    fieldref valueTrue = addValue(bytecode, OP_TRUE);

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

    addFunctionInfo("filename");
    addParameter("path", 0, false);

    addFunctionInfo("getCache");
    addParameter("label", 0, false);
    addParameter("version", 0, false);
    addParameter("key", 0, true);

    addFunctionInfo("isUptodate");
    addParameter("cacheFile", 0, false);

    addFunctionInfo("lines");
    addParameter("value", 0, false);
    addParameter("trimEmptyLastLine", valueTrue, false);

    addFunctionInfo("readFile");
    addParameter("file", 0, false);

    addFunctionInfo("setUptodate");
    addParameter("cacheFile", 0, false);

    addFunctionInfo("size");
    addParameter("value", 0, false);

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

static void nativeEcho(VM *vm)
{
    objectref prefix;
    objectref message;
    char *buffer;
    size_t length;

    prefix = InterpreterPop(vm);
    message = InterpreterPop(vm);
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
    char **argv;
    pid_t pid;
    int status;
    int pipeOut[2];
    int pipeErr[2];
    boolean echoOut;
    boolean echoErr;
    boolean failOnError;
    objectref log;
    const byte *p;
    size_t length;

    assert(returnValues <= 3);
    echoErr = InterpreterPopBoolean(vm);
    echoOut = InterpreterPopBoolean(vm);
    failOnError = InterpreterPopBoolean(vm);
    argv = createStringArray(vm, InterpreterPop(vm));

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
        printf("BUILD ERROR: Process exited with status %d.\n", status);
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

static void nativeGetCache(VM *vm, uint returnValues)
{
    objectref key;
    objectref value;
    cacheref ref;
    HashState hashState;
    byte hash[DIGEST_SIZE];
    Iterator iter;

    HashInit(&hashState);
    key = InterpreterPop(vm);
    HeapHash(vm, InterpreterPop(vm), &hashState);
    HeapHash(vm, InterpreterPop(vm), &hashState);
    HeapHash(vm, key, &hashState);
    HashFinal(&hashState, hash);
    ref = CacheGet(hash);
    if (CacheIsNewEntry(ref))
    {
        HeapIteratorInit(vm, &iter, key, true);
        while (HeapIteratorNext(&iter, &value))
        {
            if (HeapGetObjectType(vm, value) == TYPE_FILE)
            {
                CacheAddDependency(ref, HeapGetFile(vm, value));
            }
        }
    }
    if (returnValues)
    {
        InterpreterPush(vm, HeapCreateFile(vm, CacheGetFile(ref)));
    }
}

static void nativeIsUptodate(VM *vm, uint returnValues)
{
    cacheref ref = CacheGetFromFile(HeapGetFile(vm, InterpreterPop(vm)));
    if (returnValues)
    {
        InterpreterPushBoolean(vm, CacheUptodate(ref));
    }
}

static void nativeSetUptodate(VM *vm)
{
    cacheref ref = CacheGetFromFile(HeapGetFile(vm, InterpreterPop(vm)));
    CacheSetUptodate(ref);
}

void NativeInvoke(VM *vm, nativefunctionref function, uint returnValues)
{
    objectref value;
    size_t size;
    fileref file;
    const char *text;
    boolean condition;

    switch ((NativeFunction)function)
    {
    case NATIVE_ECHO:
        assert(!returnValues);
        nativeEcho(vm);
        return;

    case NATIVE_EXEC:
        nativeExec(vm, returnValues);
        return;

    case NATIVE_FAIL:
        assert(!returnValues);
        if (!HeapIsTrue(vm, InterpreterPop(vm)))
        {
            InterpreterPop(vm);
            return;
        }
        value = InterpreterPop(vm);
        LogPrintErrSZ("BUILD FAILED");
        if (!value || !HeapStringLength(vm, value))
        {
            LogErrNewline();
        }
        else
        {
            LogPrintErrSZ(": ");
            LogPrintErrObjectAutoNewline(vm, value);
        }
        TaskFailVM(vm);
        return;

    case NATIVE_FILENAME:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        assert(HeapGetObjectType(vm, value) == TYPE_FILE);
        if (returnValues)
        {
            file = HeapGetFile(vm, value);
            size = FileGetNameLength(file);
            text = FileFilename(FileGetName(file), &size);
            value = HeapCreateString(vm, text, size);
            InterpreterPush(vm, value);
        }
        return;

    case NATIVE_GETCACHE:
        assert(returnValues <= 1);
        nativeGetCache(vm, returnValues);
        return;

    case NATIVE_ISUPTODATE:
        assert(returnValues <= 1);
        nativeIsUptodate(vm, returnValues);
        return;

    case NATIVE_LINES:
        assert(returnValues <= 1);
        condition = InterpreterPopBoolean(vm);
        value = InterpreterPop(vm);
        if (returnValues)
        {
            if (HeapGetObjectType(vm, value) == TYPE_FILE)
            {
                value = readFile(vm, value);
                if (!value)
                {
                    return;
                }
            }
            assert(HeapIsString(vm, value));
            value = HeapSplitLines(vm, value, condition);
            if (!value)
            {
                return;
            }
            InterpreterPush(vm, value);
        }
        return;

    case NATIVE_READFILE:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        if (returnValues)
        {
            value = readFile(vm, value);
            if (!value)
            {
                return;
            }
            InterpreterPush(vm, value);
        }
        return;

    case NATIVE_SETUPTODATE:
        assert(!returnValues);
        nativeSetUptodate(vm);
        return;

    case NATIVE_SIZE:
        value = InterpreterPop(vm);
        if (returnValues)
        {
            assert(returnValues == 1);
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
        return;

    case NATIVE_NULL:
    case NATIVE_FUNCTION_COUNT:
        break;
    }
    assert(false);
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
