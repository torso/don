#include <stdio.h>
#include <memory.h>
#include "builder.h"
#include "bytecode.h"
#include "bytevector.h"
#include "functionindex.h"
#include "heap.h"
#include "instruction.h"
#include "interpreter.h"
#include "intvector.h"
#include "native.h"
#include "stringpool.h"

static const boolean TRACE = false;


struct RunState
{
    const byte *restrict bytecode;

    Heap heap;
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

const char *InterpreterGetString(RunState *state, ValueType type, uint value)
{
    size_t size = InterpreterGetStringSize(state, type, value);
    byte *buffer = (byte*)malloc(size + 1); /* TODO: Avoid malloc */
    assert(buffer); /* TODO: Error handling */
    InterpreterCopyString(state, type, value, buffer);
    buffer[size] = 0;
    return (char*)buffer;
}

void InterpreterFreeStringBuffer(RunState *state unused, const char *buffer)
{
    free((void*)buffer);
}

size_t InterpreterGetStringSize(RunState *state, ValueType type, uint value)
{
    size_t size;

    switch (type)
    {
    case TYPE_NULL_LITERAL:
        return 4;

    case TYPE_BOOLEAN_LITERAL:
        return value ? 4 : 5;

    case TYPE_INTEGER_LITERAL:
        size = 1;
        if ((int)value < 0)
        {
            size = 2;
            value = -value;
        }
        while (value > 9)
        {
            value /= 10;
            size++;
        }
        return size;

    case TYPE_STRING_LITERAL:
        return StringPoolGetStringLength((stringref)value);

    case TYPE_OBJECT:
        switch (HeapGetObjectType(&state->heap, value))
        {
        case TYPE_STRING:
            return HeapGetObjectSize(&state->heap, value);
        }
        assert(false);
        break;
    }
    assert(false);
    return 0;
}

void InterpreterCopyString(RunState *state, ValueType type, uint value,
                           byte *dst)
{
    size_t size;

    switch (type)
    {
    case TYPE_NULL_LITERAL:
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return;

    case TYPE_BOOLEAN_LITERAL:
        if (value)
        {
            *dst++ = 't';
            *dst++ = 'r';
            *dst++ = 'u';
            *dst++ = 'e';
        }
        else
        {
            *dst++ = 'f';
            *dst++ = 'a';
            *dst++ = 'l';
            *dst++ = 's';
            *dst++ = 'e';
        }
        return;

    case TYPE_INTEGER_LITERAL:
        size = InterpreterGetStringSize(state, type, value);
        if ((int)value < 0)
        {
            *dst++ = '-';
            size--;
            value = -value;
        }
        dst += size - 1;
        while (value)
        {
            *dst-- = (byte)('0' + value % 10);
            value /= 10;
            size--;
        }
        memset(dst - size + 1, '0', size);
        return;

    case TYPE_STRING_LITERAL:
        memcpy(dst, StringPoolGetString((stringref)value),
               StringPoolGetStringLength((stringref)value));
        return;

    case TYPE_OBJECT:
        switch (HeapGetObjectType(&state->heap, value))
        {
        case TYPE_STRING:
            memcpy(dst, HeapGetObjectData(&state->heap, value),
                   HeapGetObjectSize(&state->heap, value));
            return;
        }
        assert(false);
        break;
    }
    assert(false);
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

static boolean equals(RunState *state, ValueType type1, uint value1,
                      ValueType type2, uint value2)
{
    ValueType tempType;
    uint tempValue;
    size_t size;

    if (type1 == type2 && value1 == value2)
    {
        return true;
    }
    if (type1 == TYPE_OBJECT)
    {
    }
    else if (type2 == TYPE_OBJECT)
    {
        tempType = type1;
        type1 = type2;
        type2 = tempType;
        tempValue = value1;
        value1 = value2;
        value2 = tempValue;
    }
    else
    {
        return false;
    }
    switch (HeapGetObjectType(&state->heap, value1))
    {
    case TYPE_STRING:
        size = HeapGetObjectSize(&state->heap, value1);
        if (type2 == TYPE_STRING_LITERAL)
        {
            return size == StringPoolGetStringLength((stringref)value2) &&
                !memcmp(HeapGetObjectData(&state->heap, value1),
                        StringPoolGetString((stringref)value2), size);
        }
        if (type2 != TYPE_OBJECT ||
            HeapGetObjectType(&state->heap, value2) != TYPE_STRING)
        {
            return false;
        }
        return size == HeapGetObjectSize(&state->heap, value2) &&
            !memcmp(HeapGetObjectData(&state->heap, value1),
                    HeapGetObjectData(&state->heap, value2), size);
    }
    assert(false);
    return false;
}

static boolean addOverflow(int a, int b)
{
    return (boolean)(b < 1 ? MIN_INT - b > a : MAX_INT - b < a);
}

static boolean subOverflow(int a, int b)
{
    return (boolean)(b < 1 ? MAX_INT + b < a : MIN_INT + b > a);
}

static void pushStackFrame(RunState *state, const byte *ip, uint bp,
                           uint returnValues)
{
    IntVectorAdd(&state->callStack, (uint)(ip - state->bytecode));
    IntVectorAdd(&state->callStack, bp);
    IntVectorAdd(&state->callStack, returnValues);
}

static void popStackFrame(RunState *state, const byte **ip, uint *bp,
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
    *ip = state->bytecode + IntVectorPop(&state->callStack);
}

static void execute(RunState *state, functionref target)
{
    const byte *ip = state->bytecode + FunctionIndexGetBytecodeOffset(target);
    const byte *const baseIP = ip;
    uint bp = 0;
    uint argumentCount;
    int jumpOffset;
    uint local;
    ValueType type;
    ValueType type2;
    uint value;
    uint value2;
    size_t size1;
    size_t size2;
    byte *objectData;
    functionref function;
    nativefunctionref nativeFunction;

    local = FunctionIndexGetLocalsCount(target);
    ByteVectorSetSize(&state->typeStack, local);
    IntVectorSetSize(&state->stack, local);
    for (;;)
    {
        if (TRACE)
        {
            BytecodeDisassembleInstruction(ip, baseIP);
        }
        switch ((Instruction)*ip++)
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
            InterpreterPush(state, TYPE_INTEGER_LITERAL, BytecodeReadUint(&ip));
            break;

        case OP_STRING:
            InterpreterPush(state, TYPE_STRING_LITERAL, BytecodeReadUint(&ip));
            break;

        case OP_LOAD:
            local = BytecodeReadUint16(&ip);
            InterpreterPush(state, ByteVectorGet(&state->typeStack, bp + local),
                            IntVectorGet(&state->stack, bp + local));
            break;

        case OP_STORE:
            local = BytecodeReadUint16(&ip);
            pop(state, &type, &value);
            ByteVectorSet(&state->typeStack, bp + local, type);
            IntVectorSet(&state->stack, bp + local, value);
            break;

        case OP_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL,
                            equals(state, type, value, type2, value2));
            break;

        case OP_NOT_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL,
                            !equals(state, type, value, type2, value2));
            break;

        case OP_LESS_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 <= (int)value);
            break;

        case OP_GREATER_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 >= (int)value);
            break;

        case OP_LESS:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 < (int)value);
            break;

        case OP_GREATER:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 > (int)value);
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

        case OP_CONCAT:
            pop2(state, &type, &value, &type2, &value2);
            size1 = InterpreterGetStringSize(state, type2, value2);
            size2 = InterpreterGetStringSize(state, type, value);
            objectData = HeapAlloc(&state->heap, TYPE_STRING, size1 + size2);
            if (!objectData)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            InterpreterCopyString(state, type2, value2, objectData);
            InterpreterCopyString(state, type, value, objectData + size1);
            InterpreterPush(state, TYPE_OBJECT,
                            HeapFinishAlloc(&state->heap, objectData));
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            ip += jumpOffset;
            break;

        case OP_BRANCH_FALSE:
            assert(InterpreterPeekType(state) == TYPE_BOOLEAN_LITERAL);
            jumpOffset = BytecodeReadInt(&ip);
            if (!popValue(state))
            {
                ip += jumpOffset;
            }
            break;

        case OP_RETURN:
            assert(IntVectorSize(&state->callStack));
            popStackFrame(state, &ip, &bp, *ip++);
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
            function = (functionref)BytecodeReadUint(&ip);
            argumentCount = BytecodeReadUint16(&ip);
            value = FunctionIndexGetLocalsCount(function);
            assert(argumentCount == value); /* TODO */
            value2 = *ip++;
            pushStackFrame(state, ip, bp, value2);
            ip = state->bytecode + FunctionIndexGetBytecodeOffset(function);
            bp = (uint)IntVectorSize(&state->stack) - value;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = (nativefunctionref)*ip++;
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == NativeGetParameterCount(nativeFunction)); /* TODO */
            NativeInvoke(state, nativeFunction, *ip++);
            break;
        }
    }
}

ErrorCode InterpreterExecute(const byte *bytecode, functionref target)
{
    RunState state;

    state.bytecode = bytecode;
    state.error = HeapInit(&state.heap);
    if (state.error)
    {
        return state.error;
    }
    state.error = IntVectorInit(&state.callStack);
    if (state.error)
    {
        HeapDispose(&state.heap);
        return state.error;
    }
    state.error = ByteVectorInit(&state.typeStack);
    if (state.error)
    {
        HeapDispose(&state.heap);
        IntVectorDispose(&state.callStack);
        return state.error;
    }
    state.error = IntVectorInit(&state.stack);
    if (state.error)
    {
        HeapDispose(&state.heap);
        IntVectorDispose(&state.callStack);
        ByteVectorDispose(&state.typeStack);
        return state.error;
    }

    execute(&state, target);

    HeapDispose(&state.heap);
    IntVectorDispose(&state.callStack);
    ByteVectorDispose(&state.typeStack);
    IntVectorDispose(&state.stack);

    return state.error;
}
