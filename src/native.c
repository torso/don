#include "builder.h"
#include "heap.h"
#include "interpreter.h"
#include "native.h"
#include "stringpool.h"

#define TOTAL_PARAMETER_COUNT 2

typedef enum
{
    NATIVE_ECHO,
    NATIVE_SIZE,

    NATIVE_FUNCTION_COUNT
} NativeFunction;

typedef struct
{
    stringref name;
    uint parameterCount;
    uint minimumArgumentCount;
    stringref parameterNames[1];
} FunctionInfo;

static byte functionInfo[
    (sizeof(FunctionInfo) -
     sizeof(stringref)) * NATIVE_FUNCTION_COUNT +
    sizeof(stringref) * TOTAL_PARAMETER_COUNT];
static uint functionIndex[NATIVE_FUNCTION_COUNT];

static byte *initFunctionInfo = functionInfo;
static uint initFunctionIndex = 0;

static void addFunctionInfo(const char *name,
                            uint parameterCount, uint minimumArgumentCount,
                            const char **parameterNames)
{
    FunctionInfo *info = (FunctionInfo*)initFunctionInfo;
    stringref *pnames;
    if (!info)
    {
        return;
    }
    functionIndex[initFunctionIndex++] = (uint)(initFunctionInfo - functionInfo);
    pnames = info->parameterNames;
    initFunctionInfo += sizeof(FunctionInfo) - sizeof(stringref) +
        sizeof(stringref) * parameterCount;
    assert(initFunctionInfo <= &functionInfo[sizeof(functionInfo)]);

    info->name = StringPoolAdd(name);
    info->parameterCount = parameterCount;
    info->minimumArgumentCount = minimumArgumentCount;
    if (!info->name)
    {
        initFunctionInfo = null;
        return;
    }
    while (parameterCount--)
    {
        *pnames = StringPoolAdd(*parameterNames);
        if (!*pnames)
        {
            initFunctionInfo = null;
            return;
        }
        pnames++;
        parameterNames++;
    }
}

static pure const FunctionInfo *getFunctionInfo(nativefunctionref function)
{
    assert(function >= 0);
    assert(function < NATIVE_FUNCTION_COUNT);
    return (FunctionInfo*)&functionInfo[functionIndex[function]];
}

ErrorCode NativeInit(void)
{
    static const char *echoParameters[] = {"message"};
    static const char *sizeParameters[] = {"collection"};
    addFunctionInfo("echo", 1, 1, echoParameters);
    addFunctionInfo("size", 1, 1, sizeParameters);
    return initFunctionInfo ? NO_ERROR : OUT_OF_MEMORY;
}

void NativeInvoke(RunState *state, nativefunctionref function,
                  uint returnValues)
{
    Heap *heap;
    ValueType type;
    uint value;
    const char *buffer;

    switch ((NativeFunction)function)
    {
    case NATIVE_ECHO:
        assert(!returnValues);
        InterpreterPop(state, &type, &value);
        buffer = InterpreterGetString(state, type, value);
        printf("%s\n", buffer);
        InterpreterFreeStringBuffer(state, buffer);
        return;

    case NATIVE_SIZE:
        InterpreterPop(state, &type, &value);
        if (returnValues)
        {
            assert(returnValues == 1);
            heap = InterpreterGetHeap(state);
            assert(type == TYPE_OBJECT);
            assert(HeapCollectionSize(heap, value) <= MAX_INT);
            InterpreterPush(state, TYPE_INTEGER_LITERAL,
                            (uint)HeapCollectionSize(heap, value));
        }
        return;

    case NATIVE_FUNCTION_COUNT:
        break;
    }
    assert(false);
}

nativefunctionref NativeFindFunction(stringref name)
{
    int i;
    for (i = 0; i < NATIVE_FUNCTION_COUNT; i++)
    {
        if (NativeGetName(i) == name)
        {
            return i;
        }
    }
    return -1;
}

stringref NativeGetName(nativefunctionref function)
{
    return getFunctionInfo(function)->name;
}

uint NativeGetParameterCount(nativefunctionref function)
{
    return getFunctionInfo(function)->parameterCount;
}

uint NativeGetMinimumArgumentCount(nativefunctionref function)
{
    return getFunctionInfo(function)->minimumArgumentCount;
}

const stringref *NativeGetParameterNames(nativefunctionref function)
{
    return getFunctionInfo(function)->parameterNames;
}
