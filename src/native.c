#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "interpreterstate.h"
#include "value.h"
#include "native.h"
#include "instruction.h"

#define NATIVE_FUNCTION_COUNT 1

static stringref echoParameterNames[1];
static stringref functionNames[NATIVE_FUNCTION_COUNT];
static uint bytecodeOffsets[NATIVE_FUNCTION_COUNT];

ErrorCode NativeInit(void)
{
    functionNames[0] = StringPoolAdd("echo");
    return functionNames[0] ? NO_ERROR : OUT_OF_MEMORY;
}

void NativeInvoke(RunState *state, nativefunctionref function)
{
    if (function == 0)
    {
        ValuePrint(state, ValueGetOffset(state->bp, 0));
        printf("\n");
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

ErrorCode NativeWriteBytecode(bytevector *restrict bytecode,
                              bytevector *restrict valueBytecode)
{
    uint function;
    uint parameter;
    uint parameterCount;
    ErrorCode error;

    for (function = 0; function < NATIVE_FUNCTION_COUNT; function++)
    {
        bytecodeOffsets[function] = ByteVectorSize(bytecode);
        parameterCount = NativeGetParameterCount((nativefunctionref)function);
        error = ByteVectorAddPackUint(bytecode, parameterCount);
        if (error)
        {
            return error;
        }
        for (parameter = 0; parameter < parameterCount; parameter++)
        {
            if ((error = ByteVectorAddPackUint(
                     bytecode, ByteVectorSize(valueBytecode))) ||
                (error = ByteVectorAdd(valueBytecode, DATAOP_PARAMETER)) ||
                (error = ByteVectorAddPackUint(valueBytecode,
                                               (uint)StringPoolAdd("dummy"))))
            {
                return error;
            }
        }
        error = ByteVectorAdd(bytecode, (byte)-1);
        if (error)
        {
            return error;
        }
    }
    return NO_ERROR;
}
