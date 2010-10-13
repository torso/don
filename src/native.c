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

#define TOTAL_PARAMETER_COUNT 8

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
    ParameterInfo parameterInfo[1];
} FunctionInfo;

static byte functionInfo[
    (sizeof(FunctionInfo) -
     sizeof(ParameterInfo)) * NATIVE_FUNCTION_COUNT +
    sizeof(ParameterInfo) * TOTAL_PARAMETER_COUNT];
static uint functionIndex[NATIVE_FUNCTION_COUNT];

static byte *initFunctionInfo = functionInfo;
static FunctionInfo *currentFunctionInfo;
static uint initFunctionIndex = 1;
static boolean failed;


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

    if (!field || ByteVectorAdd(bytecode, op))
    {
        failed = true;
        return 0;
    }
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
    failed = failed || currentFunctionInfo->name == 0;
}

static void addParameter(const char *name, fieldref value)
{
    ParameterInfo *info = (ParameterInfo*)initFunctionInfo;

    info->name = StringPoolAdd(name);
    info->value = value;
    failed = failed || info->name == 0;
    currentFunctionInfo->parameterCount++;
    initFunctionInfo += sizeof(ParameterInfo);
}

ErrorCode NativeInit(bytevector *bytecode)
{
    fieldref valueNull = addValue(bytecode, OP_NULL);
    fieldref valueTrue = addValue(bytecode, OP_TRUE);

    addFunctionInfo("echo");
    addParameter("message", 0);

    addFunctionInfo("exec");
    addParameter("command", 0);

    addFunctionInfo("fail");
    addParameter("message", valueNull);
    addParameter("condition", valueTrue);

    addFunctionInfo("filename");
    addParameter("path", 0);

    addFunctionInfo("lines");
    addParameter("value", 0);
    addParameter("trimEmptyLastLine", valueTrue);

    addFunctionInfo("readFile");
    addParameter("file", 0);

    addFunctionInfo("size");
    addParameter("value", 0);

    assert(initFunctionInfo == functionInfo + sizeof(functionInfo));
    return failed ? OUT_OF_MEMORY : NO_ERROR;
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
    if (!strings)
    {
        return null;
    }

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

    vm->error = FileMMap(file, (const byte**)&text, &size);
    if (vm->error)
    {
        return vm->error;
    }
    if (!size)
    {
        InterpreterPush(vm, vm->emptyList);
        return vm->error;
    }
    return HeapCreateWrappedString(vm, text, size);
}

ErrorCode NativeInvoke(VM *vm, nativefunctionref function, uint returnValues)
{
    objectref value;
    size_t size;
    pid_t pid;
    int status;
    char **argv;
    int pipeOut[2];
    int pipeErr[2];
    fileref file;
    const char *text;
    boolean condition;

    switch ((NativeFunction)function)
    {
    case NATIVE_ECHO:
        assert(!returnValues);
        value = InterpreterPop(vm);
        size = HeapStringLength(vm, value);
        return LogPrintObjectAutoNewline(vm, value);

    case NATIVE_EXEC:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        argv = createStringArray(vm, value);
        if (!argv)
        {
            return OUT_OF_MEMORY;
        }

        status = pipe(pipeOut);
        if (status < 0)
        {
            /* TODO: Error handling. */
            return OUT_OF_MEMORY;
        }
        status = pipe(pipeErr);
        if (status < 0)
        {
            /* TODO: Error handling. */
            return OUT_OF_MEMORY;
        }

        pid = fork();
        if (!pid)
        {
            close(pipeOut[0]);
            close(pipeErr[0]);

            status = dup2(pipeOut[1], STDOUT_FILENO);
            if (status < 0)
            {
                /* TODO: Error handling. */
                return false;
            }
            close(pipeOut[1]);
            status = dup2(pipeErr[1], STDERR_FILENO);
            if (status < 0)
            {
                /* TODO: Error handling. */
                return false;
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
            /* TODO: Error handling. */
            return OUT_OF_MEMORY;
        }

        LogConsumePipes(pipeOut[0], pipeErr[0]);
        LogAutoNewline();

        pid = waitpid(pid, &status, 0);
        if (pid < 0)
        {
            /* TODO: Error handling. */
            return OUT_OF_MEMORY;
        }
        if (returnValues)
        {
            InterpreterPush(vm, HeapBoxInteger(vm, status));
        }
        return NO_ERROR;

    case NATIVE_FAIL:
        assert(!returnValues);
        if (!HeapIsTrue(vm, InterpreterPop(vm)))
        {
            InterpreterPop(vm);
            return NO_ERROR;
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
        return ERROR_FAIL;

    case NATIVE_FILENAME:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        assert(HeapGetObjectType(vm, value) == TYPE_FILE);
        if (returnValues)
        {
            file = HeapGetFile(vm, value);
            size = FileGetNameLength(file);
            text = FileFilename(FileGetName(file), &size);
            if (!text)
            {
                return OUT_OF_MEMORY;
            }
            value = HeapCreateString(vm, text, size);
            if (!value)
            {
                return OUT_OF_MEMORY;
            }
            InterpreterPush(vm, value);
        }
        return NO_ERROR;

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
                    return vm->error;
                }
            }
            assert(HeapIsString(vm, value));
            value = HeapSplitLines(vm, value, condition);
            if (!value)
            {
                return vm->error;
            }
            InterpreterPush(vm, value);
        }
        return NO_ERROR;

    case NATIVE_READFILE:
        assert(returnValues <= 1);
        value = InterpreterPop(vm);
        if (returnValues)
        {
            value = readFile(vm, value);
            if (!value)
            {
                return vm->error;
            }
            InterpreterPush(vm, value);
            return vm->error;
        }
        return NO_ERROR;

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
        return NO_ERROR;

    case NATIVE_NULL:
    case NATIVE_FUNCTION_COUNT:
        break;
    }
    assert(false);
    return NO_ERROR;
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
