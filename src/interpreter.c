#include <stdio.h>
#include "builder.h"
#include "bytecode.h"
#include "bytevector.h"
#include "instruction.h"
#include "intvector.h"
#include "interpreter.h"
#include "native.h"
#include "functionindex.h"

static const boolean TRACE = false;


struct RunState
{
    const bytevector *restrict bytecode;

    intvector callStack;
    bytevector typeStack;
    intvector stack;
    ErrorCode error;
};


static boolean setError(RunState *state, ErrorCode error)
{
    state->error = error;
    return error ? true : false;
}

ValueType InterpreterPeekType(RunState *state)
{
    return ByteVectorPeek(&state->typeStack);
}

void InterpreterPop(RunState *state, ValueType *type, uint *value)
{
    *type = ByteVectorPop(&state->typeStack);
    *value = IntVectorPop(&state->stack);
}

static uint popValue(RunState *state)
{
    ByteVectorPop(&state->typeStack);
    return IntVectorPop(&state->stack);
}

static void pop(RunState *state, ValueType *type, uint *value)
{
    InterpreterPop(state, type, value);
}

static void pop2(RunState *state, ValueType *type1, uint *value1,
                 ValueType *type2, uint *value2)
{
    InterpreterPop(state, type1, value1);
    InterpreterPop(state, type2, value2);
}

boolean InterpreterPush(RunState *state, ValueType type, uint value)
{
    return !setError(state, ByteVectorAdd(&state->typeStack, type)) &&
        !setError(state, IntVectorAdd(&state->stack, value));
}

static boolean equals(ValueType type1, uint value1,
                      ValueType type2, uint value2)
{
    return type1 == type2 && value1 == value2;
}

static boolean addOverflow(int a, int b)
{
    return (boolean)(b < 1 ? MIN_INT - b > a : MAX_INT - b < a);
}

static boolean subOverflow(int a, int b)
{
    return (boolean)(b < 1 ? MAX_INT + b < a : MIN_INT + b > a);
}

static void pushStackFrame(RunState *state, uint ip, uint bp, uint returnValues)
{
    IntVectorAdd(&state->callStack, ip);
    IntVectorAdd(&state->callStack, bp);
    IntVectorAdd(&state->callStack, returnValues);
}

static void popStackFrame(RunState *state, uint *ip, uint *bp,
                          uint returnValues)
{
    uint expectedReturnValues = IntVectorPop(&state->callStack);

    ByteVectorCopy(&state->typeStack,
                   ByteVectorSize(&state->typeStack) - returnValues,
                   &state->typeStack,
                   *bp,
                   expectedReturnValues);
    IntVectorCopy(&state->stack,
                  IntVectorSize(&state->stack) - returnValues,
                  &state->stack,
                  *bp,
                  expectedReturnValues);
    ByteVectorSetSize(&state->typeStack, *bp + expectedReturnValues);
    IntVectorSetSize(&state->stack, *bp + expectedReturnValues);

    *bp = IntVectorPop(&state->callStack);
    *ip = IntVectorPop(&state->callStack);
}

static void execute(RunState *state, functionref target)
{
    uint ip = FunctionIndexGetBytecodeOffset(target);
    uint bp = 0;
    uint argumentCount;
    uint jumpOffset;
    uint local;
    ValueType type;
    ValueType type2;
    uint value;
    uint value2;
    functionref function;
    nativefunctionref nativeFunction;

    local = FunctionIndexGetLocalsCount(target);
    ByteVectorSetSize(&state->typeStack, local);
    IntVectorSetSize(&state->stack, local);
    for (;;)
    {
        if (TRACE)
        {
            BytecodeDisassembleInstruction(state->bytecode, ip);
        }
        switch ((Instruction)(int)ByteVectorRead(state->bytecode, &ip))
        {
        case OP_NULL:
            InterpreterPush(state, TYPE_NULL_LITERAL, 0);
            break;

        case OP_TRUE:
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, 1);
            break;

        case OP_FALSE:
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, 0);
            break;

        case OP_INTEGER:
            InterpreterPush(state, TYPE_INTEGER_LITERAL,
                            (uint)ByteVectorReadInt(state->bytecode, &ip));
            break;

        case OP_STRING:
            InterpreterPush(state, TYPE_STRING_LITERAL,
                            ByteVectorReadUint(state->bytecode, &ip));
            break;

        case OP_LOAD:
            local = ByteVectorReadUint16(state->bytecode, &ip);
            InterpreterPush(state, ByteVectorGet(&state->typeStack, bp + local),
                            IntVectorGet(&state->stack, bp + local));
            break;

        case OP_STORE:
            local = ByteVectorReadUint16(state->bytecode, &ip);
            pop(state, &type, &value);
            ByteVectorSet(&state->typeStack, bp + local, type);
            IntVectorSet(&state->stack, bp + local, value);
            break;

        case OP_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL,
                            equals(type, value, type2, value2));
            break;

        case OP_NOT_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL,
                            !equals(type, value, type2, value2));
            break;

        case OP_ADD:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            assert(!addOverflow((int)value, (int)value2));
            InterpreterPush(state, TYPE_INTEGER_LITERAL, value + value2);
            break;

        case OP_SUB:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            assert(!subOverflow((int)value2, (int)value));
            InterpreterPush(state, TYPE_INTEGER_LITERAL, value2 - value);
            break;

        case OP_JUMP:
            jumpOffset = (uint)ByteVectorReadInt(state->bytecode, &ip);
            ip += jumpOffset;
            break;

        case OP_BRANCH_FALSE:
            assert(InterpreterPeekType(state) == TYPE_BOOLEAN_LITERAL);
            jumpOffset = (uint)ByteVectorReadInt(state->bytecode, &ip);
            if (!popValue(state))
            {
                ip += jumpOffset;
            }
            break;

        case OP_RETURN:
            assert(IntVectorSize(&state->callStack));
            popStackFrame(state, &ip, &bp,
                          ByteVectorReadPackUint(state->bytecode, &ip));
            break;

        case OP_RETURN_VOID:
            if (!IntVectorSize(&state->callStack))
            {
                assert(IntVectorSize(&state->stack) ==
                       FunctionIndexGetLocalsCount(target));
                return;
            }
            popStackFrame(state, &ip, &bp, 0);
            break;

        case OP_INVOKE:
            function = (functionref)ByteVectorReadPackUint(state->bytecode, &ip);
            argumentCount = ByteVectorReadPackUint(state->bytecode, &ip);
            value = FunctionIndexGetLocalsCount(function);
            assert(argumentCount == value); /* TODO */
            pushStackFrame(state, ip, bp,
                           ByteVectorReadPackUint(state->bytecode, &ip));
            ip = FunctionIndexGetBytecodeOffset(function);
            bp = IntVectorSize(&state->stack) - value;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = (nativefunctionref)ByteVectorRead(state->bytecode, &ip);
            argumentCount = ByteVectorReadPackUint(state->bytecode, &ip);
            assert(argumentCount == NativeGetParameterCount(nativeFunction)); /* TODO */
            NativeInvoke(state, nativeFunction,
                         ByteVectorReadPackUint(state->bytecode, &ip));
            break;
        }
    }
}

ErrorCode InterpreterExecute(const bytevector *bytecode, functionref target)
{
    RunState state;

    state.bytecode = bytecode;
    state.error = IntVectorInit(&state.callStack);
    if (state.error)
    {
        return state.error;
    }
    state.error = ByteVectorInit(&state.typeStack);
    if (state.error)
    {
        IntVectorDispose(&state.callStack);
        return state.error;
    }
    state.error = IntVectorInit(&state.stack);
    if (state.error)
    {
        IntVectorDispose(&state.callStack);
        ByteVectorDispose(&state.typeStack);
        return state.error;
    }

    execute(&state, target);

    IntVectorDispose(&state.callStack);
    ByteVectorDispose(&state.typeStack);
    IntVectorDispose(&state.stack);

    return state.error;
}
