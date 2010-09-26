#include "builder.h"
#include "heap.h"
#include "interpreter.h"
#include "native.h"
#include "stringpool.h"

#define NATIVE_FUNCTION_COUNT 1

static stringref echoParameterNames[1];
static stringref functionNames[NATIVE_FUNCTION_COUNT];
static uint bytecodeOffsets[NATIVE_FUNCTION_COUNT];

ErrorCode NativeInit(void)
{
    functionNames[0] = StringPoolAdd("echo");
    return functionNames[0] ? NO_ERROR : OUT_OF_MEMORY;
}

void NativeInvoke(RunState *state, nativefunctionref function,
                  uint returnValues)
{
    ValueType type;
    uint value;
    const char *buffer;

    if (function == 0)
    {
        assert(!returnValues);
        InterpreterPop(state, &type, &value);
        buffer = InterpreterGetString(state, type, value);
        printf("%s\n", buffer);
        InterpreterFreeStringBuffer(state, buffer);
        return;
    }
    assert(false);
}

nativefunctionref NativeFindFunction(stringref name)
{
    int i;
    for (i = 0; i < NATIVE_FUNCTION_COUNT; i++)
    {
        if (name == functionNames[i])
        {
            return i;
        }
    }
    return -1;
}

nativefunctionref NativeGetFromBytecodeOffset(uint bytecodeOffset)
{
    int i;
    for (i = 0; i < NATIVE_FUNCTION_COUNT; i++)
    {
        if (bytecodeOffset == bytecodeOffsets[i])
        {
            return i;
        }
    }
    assert(false);
    return -1;
}

stringref NativeGetName(nativefunctionref function)
{
    assert(function >= 0);
    assert(function < NATIVE_FUNCTION_COUNT);
    return functionNames[function];
}

uint NativeGetParameterCount(nativefunctionref function)
{
    assert(function == 0);
    return 1;
}

uint NativeGetMinimumArgumentCount(nativefunctionref function)
{
    assert(function == 0);
    return 1;
}

const stringref *NativeGetParameterNames(nativefunctionref function)
{
    assert(function == 0);
    return echoParameterNames;
}

uint NativeGetBytecodeOffset(nativefunctionref function)
{
    assert(function < NATIVE_FUNCTION_COUNT);
    return bytecodeOffsets[function];
}
