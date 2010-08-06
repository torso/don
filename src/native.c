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
static uint bytecodeOffsets[NATIVE_FUNCTION_COUNT];

ErrorCode NativeInit(void)
{
    return StringPoolAdd("echo") ? NO_ERROR : OUT_OF_MEMORY;
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
    if (name == StringPoolAdd("echo"))
    {
        return 0;
    }
    return -1;
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
