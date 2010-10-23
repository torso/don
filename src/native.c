#include <memory.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "common.h"
#include "vm.h"
#include "fieldindex.h"
#include "file.h"
#include "instruction.h"
#include "interpreter.h"
#include "log.h"
#include "native.h"
#include "stringpool.h"
#include "task.h"

#define TOTAL_PARAMETER_COUNT 13

typedef enum
{
    NATIVE_NULL,
    NATIVE_ECHO,
    NATIVE_EXEC,
    NATIVE_FAIL,
    NATIVE_FILENAME,
    NATIVE_LINES,
    NATIVE_READFILE,
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

    addFunctionInfo("lines");
    addParameter("value", 0, false);
    addParameter("trimEmptyLastLine", valueTrue, false);

    addFunctionInfo("readFile");
    addParameter("file", 0, false);

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
        vm->error = LogPrintObjectAutoNewline(vm, message);
        LogSetPrefix(null, 0);
        free(buffer);
    }
    else
    {
        vm->error = LogPrintObjectAutoNewline(vm, message);
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
        TaskFailErrno();
    }
    status = pipe(pipeErr);
    if (status == -1)
    {
        TaskFailErrno();
    }

    pid = fork();
    if (!pid)
    {
        close(pipeOut[0]);
        close(pipeErr[0]);

        status = dup2(pipeOut[1], STDOUT_FILENO);
        if (status == -1)
        {
            TaskFailErrno();
        }
        close(pipeOut[1]);
        status = dup2(pipeErr[1], STDERR_FILENO);
        if (status == -1)
        {
            TaskFailErrno();
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
        TaskFailErrno();
    }
    if (failOnError && status)
    {
        vm->error = ERROR_FAIL;
        return;
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
        LogPrintSZ("BUILD FAILED");
        if (!value || !HeapStringLength(vm, value))
        {
            LogNewline();
        }
        else
        {
            LogPrintSZ(": ");
            LogPrintObjectAutoNewline(vm, value);
        }
        vm->error = ERROR_FAIL;
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
