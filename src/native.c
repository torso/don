#include <stdlib.h>
#include "builder.h"
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

    if (function == 0)
    {
        assert(!returnValues);
        InterpreterPop(state, &type, &value);
        switch (type)
        {
        case TYPE_NULL_LITERAL:
            printf("null\n");
            return;

        case TYPE_BOOLEAN_LITERAL:
            printf(value ? "true\n" : "false\n");
            return;

        case TYPE_INTEGER_LITERAL:
            printf("%d\n", value);
            return;

        case TYPE_STRING_LITERAL:
            printf("%s\n", StringPoolGetString((stringref)value));
            return;

        default:
            assert(false);
            return;
        }
    }
    else
    {
        assert(false);
    }
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
