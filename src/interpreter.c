#include <stdio.h>
#include <memory.h>
#include "builder.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "fileindex.h"
#include "functionindex.h"
#include "heap.h"
#include "instruction.h"
#include "interpreter.h"
#include "intvector.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"

static const boolean TRACE = false;


struct RunState
{
    const byte *restrict bytecode;

    Heap heap;
    ValueType *fieldTypes;
    uint *fields;
    intvector callStack;
    bytevector typeStack;
    intvector stack;
    ErrorCode error;

    bytevector *pipeOut;
    bytevector *pipeErr;
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

static void peek(RunState *state, ValueType *type, uint *value)
{
    *type = ByteVectorPeek(&state->typeStack);
    *value = IntVectorPeek(&state->stack);
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

static void storeLocal(RunState *state, uint bp, uint16 local,
                       ValueType type, uint value)
{
    ByteVectorSet(&state->typeStack, bp + local, type);
    IntVectorSet(&state->stack, bp + local, value);
}

static boolean boxInt(RunState *state, ObjectType type, uint *value)
{
    byte *objectData = HeapAlloc(&state->heap, type, sizeof(int));
    if (!objectData)
    {
        state->error = OUT_OF_MEMORY;
        return false;
    }
    *(uint*)objectData = *value;
    *value = HeapFinishAlloc(&state->heap, objectData);
    return true;
}

static boolean box(RunState *state, ValueType type, uint *value)
{
    size_t size;
    byte *objectData;

    switch (type)
    {
    case TYPE_NULL_LITERAL:
        assert(!*value);
        return true;

    case TYPE_BOOLEAN_LITERAL:
        objectData = HeapAlloc(&state->heap, TYPE_BOOLEAN, sizeof(boolean));
        if (!objectData)
        {
            state->error = OUT_OF_MEMORY;
            return false;
        }
        *(boolean*)objectData = (boolean)*value;
        *value = HeapFinishAlloc(&state->heap, objectData);
        return true;

    case TYPE_INTEGER_LITERAL:
        return boxInt(state, TYPE_INTEGER, value);

    case TYPE_STRING_LITERAL:
        size = InterpreterGetStringSize(state, type, *value);
        objectData = HeapAlloc(&state->heap, TYPE_STRING, size);
        if (!objectData)
        {
            state->error = OUT_OF_MEMORY;
            return false;
        }
        InterpreterCopyString(state, type, *value, objectData);
        *value = HeapFinishAlloc(&state->heap, objectData);
        return true;

    case TYPE_FILE_LITERAL:
        return boxInt(state, TYPE_FILE, value);

    case TYPE_OBJECT:
        return true;
    }
    assert(false);
    return false;
}

static void unbox(RunState *state, ValueType *type, uint *value)
{
    if (*type != TYPE_OBJECT)
    {
        return;
    }
    switch (HeapGetObjectType(&state->heap, *value))
    {
    case TYPE_BOOLEAN:
        *type = TYPE_BOOLEAN_LITERAL;
        *value = *(boolean*)HeapGetObjectData(&state->heap, *value);
        return;

    case TYPE_INTEGER:
        *type = TYPE_INTEGER_LITERAL;
        *value = *(uint*)HeapGetObjectData(&state->heap, *value);
        return;

    case TYPE_FILE:
        *type = TYPE_FILE_LITERAL;
        *value = *(uint*)HeapGetObjectData(&state->heap, *value);
        return;

    case TYPE_STRING:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_FILESET:
    case TYPE_ITERATOR:
        return;

    default:
        assert(false);
        return;
    }
}

static uint createRange(RunState *state, int low, int high)
{
    byte *objectData;
    int *p;

    assert((int)low <= (int)high); /* TODO: Reverse range */
    assert(!subOverflow((int)high, (int)low));
    objectData = HeapAlloc(&state->heap, TYPE_INTEGER_RANGE, 2 * sizeof(int));
    if (!objectData)
    {
        state->error = OUT_OF_MEMORY;
        return 0;
    }
    p = (int*)objectData;
    *p++ = low;
    *p = high;
    return HeapFinishAlloc(&state->heap, objectData);
}

static uint createIterator(RunState *state, uint object)
{
    Iterator *iter = (Iterator *)HeapAlloc(&state->heap, TYPE_ITERATOR,
                                           sizeof(Iterator));
    HeapCollectionIteratorInit(&state->heap, iter, object);
    return HeapFinishAlloc(&state->heap, (byte*)iter);
}

static boolean equals(RunState *state, ValueType type1, uint value1,
                      ValueType type2, uint value2)
{
    Iterator iter1;
    Iterator iter2;
    ValueType tempType;
    uint tempValue;
    size_t size;
    boolean success;

    unbox(state, &type1, &value1);
    unbox(state, &type2, &value2);
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
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ITERATOR:
        assert(false);
        return false;

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

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_FILESET:
        if (type2 != TYPE_OBJECT ||
            !HeapIsCollection(&state->heap, value2) ||
            HeapCollectionSize(&state->heap, value1) !=
            HeapCollectionSize(&state->heap, value2))
        {
            return false;
        }
        HeapCollectionIteratorInit(&state->heap, &iter1, value1);
        HeapCollectionIteratorInit(&state->heap, &iter2, value2);
        while (HeapIteratorNext(&iter1, &type1, &value1))
        {
            success = HeapIteratorNext(&iter2, &type2, &value2);
            assert(success);
            if (!equals(state, type1, value1, type2, value2))
            {
                return false;
            }
        }
        return true;
    }
    assert(false);
    return false;
}

Heap *InterpreterGetHeap(RunState *state)
{
    return &state->heap;
}

bytevector *InterpreterGetPipeOut(RunState *state)
{
    return state->pipeOut;
}

bytevector *InterpreterGetPipeErr(RunState *state)
{
    return state->pipeErr;
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
    Iterator iter;
    size_t size;

    unbox(state, &type, &value);
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

    case TYPE_FILE_LITERAL:
        return strlen(FileIndexGetName((fileref)value));

    case TYPE_OBJECT:
        switch (HeapGetObjectType(&state->heap, value))
        {
        case TYPE_STRING:
            return HeapGetObjectSize(&state->heap, value);

        case TYPE_EMPTY_LIST:
        case TYPE_ARRAY:
        case TYPE_INTEGER_RANGE:
        case TYPE_FILESET:
            size = HeapCollectionSize(&state->heap, value);
            if (size)
            {
                size--;
            }
            size = size * 2 + 2;
            HeapCollectionIteratorInit(&state->heap, &iter, value);
            while (HeapIteratorNext(&iter, &type, &value))
            {
                size += InterpreterGetStringSize(state, type, value);
            }
            return size;

        case TYPE_BOOLEAN:
        case TYPE_INTEGER:
        case TYPE_FILE:
        case TYPE_ITERATOR:
            break;
        }
        break;
    }
    assert(false);
    return 0;
}

byte *InterpreterCopyString(RunState *state, ValueType type, uint value,
                            byte *dst)
{
    Iterator iter;
    size_t size;
    boolean first;

    unbox(state, &type, &value);
    switch (type)
    {
    case TYPE_NULL_LITERAL:
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return dst;

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
        return dst;

    case TYPE_INTEGER_LITERAL:
        if (!value)
        {
            *dst++ = '0';
            return dst;
        }
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
        }
        return dst + size + 1;

    case TYPE_STRING_LITERAL:
        size = StringPoolGetStringLength((stringref)value);
        memcpy(dst, StringPoolGetString((stringref)value), size);
        return dst + size;

    case TYPE_FILE_LITERAL:
        size = strlen(FileIndexGetName((fileref)value));
        memcpy(dst, FileIndexGetName((fileref)value), size);
        return dst + size;

    case TYPE_OBJECT:
        switch (HeapGetObjectType(&state->heap, value))
        {
        case TYPE_STRING:
            size = HeapGetObjectSize(&state->heap, value);
            memcpy(dst, HeapGetObjectData(&state->heap, value), size);
            return dst + size;

        case TYPE_EMPTY_LIST:
        case TYPE_ARRAY:
        case TYPE_INTEGER_RANGE:
        case TYPE_FILESET:
            *dst++ = '[';
            first = true;
            HeapCollectionIteratorInit(&state->heap, &iter, value);
            while (HeapIteratorNext(&iter, &type, &value))
            {
                if (!first)
                {
                    *dst++ = ',';
                    *dst++ = ' ';
                }
                first = false;
                dst = InterpreterCopyString(state, type, value, dst);
            }
            *dst++ = ']';
            return dst;

        case TYPE_BOOLEAN:
        case TYPE_INTEGER:
        case TYPE_FILE:
        case TYPE_ITERATOR:
            break;
        }
        break;
    }
    assert(false);
    return null;
}

static void createString(RunState *state, bytevector *data,
                         ValueType *type, uint *value)
{
    /* TODO: Avoid copying data. */
    size_t size = ByteVectorSize(data);
    byte *objectData;

    if (!size)
    {
        *type = TYPE_STRING_LITERAL;
        *value = (uint)StringPoolAdd("");
        assert(*value);
        return;
    }

    objectData = HeapAlloc(&state->heap, TYPE_STRING, size);
    if (!objectData)
    {
        state->error = OUT_OF_MEMORY;
        return;
    }
    memcpy(objectData, ByteVectorGetPointer(data, 0), size);
    *type = TYPE_OBJECT;
    *value = HeapFinishAlloc(&state->heap, objectData);
}

static void pushStackFrame(RunState *state, const byte **ip, uint *bp,
                           functionref function, uint returnValues)
{
    uint localsCount;
    IntVectorAdd(&state->callStack, (uint)(*ip - state->bytecode));
    IntVectorAdd(&state->callStack, *bp);
    IntVectorAdd(&state->callStack, returnValues);
    *ip = state->bytecode + FunctionIndexGetBytecodeOffset(function);
    *bp = (uint)IntVectorSize(&state->stack) - FunctionIndexGetParameterCount(function);
    localsCount = FunctionIndexGetLocalsCount(function);
    ByteVectorSetSize(&state->typeStack, *bp + localsCount);
    IntVectorSetSize(&state->stack, *bp + localsCount);
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
    const byte *baseIP = ip;
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
    Iterator *iter;
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

        case OP_EMPTY_LIST:
            InterpreterPush(state, TYPE_OBJECT, state->heap.emptyList);
            break;

        case OP_LIST:
            size1 = BytecodeReadUint(&ip);
            objectData = HeapAlloc(&state->heap, TYPE_ARRAY,
                                   size1 * sizeof(uint));
            if (!objectData)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            objectData += size1 * sizeof(uint);
            while (size1--)
            {
                pop(state, &type, &value);
                if (!box(state, type, &value))
                {
                    return;
                }
                objectData -= sizeof(uint);
                *(uint*)objectData = value;
            }
            InterpreterPush(state, TYPE_OBJECT,
                            HeapFinishAlloc(&state->heap, objectData));
            break;

        case OP_FILESET:
            state->error = HeapCreateFilesetGlob(
                &state->heap,
                StringPoolGetString((stringref)BytecodeReadUint(&ip)),
                &type, &value);
            if (state->error)
            {
                return;
            }
            InterpreterPush(state, type, value);
            break;

        case OP_POP:
            pop(state, &type, &value);
            break;

        case OP_DUP:
            peek(state, &type, &value);
            InterpreterPush(state, type, value);
            break;

        case OP_LOAD:
            local = BytecodeReadUint16(&ip);
            InterpreterPush(state, ByteVectorGet(&state->typeStack, bp + local),
                            IntVectorGet(&state->stack, bp + local));
            break;

        case OP_STORE:
            pop(state, &type, &value);
            storeLocal(state, bp, BytecodeReadUint16(&ip), type, value);
            break;

        case OP_LOAD_FIELD:
            value = BytecodeReadUint(&ip);
            InterpreterPush(state,
                            state->fieldTypes[value], state->fields[value]);
            break;

        case OP_STORE_FIELD:
            value = BytecodeReadUint(&ip);
            pop(state, &state->fieldTypes[value], &state->fields[value]);
            break;

        case OP_CAST_BOOLEAN:
            assert(InterpreterPeekType(state) == TYPE_BOOLEAN_LITERAL);
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
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 <= (int)value);
            break;

        case OP_GREATER_EQUALS:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 >= (int)value);
            break;

        case OP_LESS:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 < (int)value);
            break;

        case OP_GREATER:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, (int)value2 > (int)value);
            break;

        case OP_NOT:
            pop(state, &type, &value);
            unbox(state, &type, &value);
            assert(type == TYPE_BOOLEAN_LITERAL);
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL, !value);
            break;

        case OP_NEG:
            pop(state, &type, &value);
            unbox(state, &type, &value);
            assert(type == TYPE_INTEGER_LITERAL);
            assert((int)value != MIN_INT);
            InterpreterPush(state, TYPE_INTEGER_LITERAL, -value);
            break;

        case OP_INV:
            pop(state, &type, &value);
            unbox(state, &type, &value);
            assert(type == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_INTEGER_LITERAL, ~value);
            break;

        case OP_ADD:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            assert(!addOverflow((int)value, (int)value2));
            InterpreterPush(state, TYPE_INTEGER_LITERAL, value + value2);
            break;

        case OP_SUB:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            assert(!subOverflow((int)value2, (int)value));
            InterpreterPush(state, TYPE_INTEGER_LITERAL, value2 - value);
            break;

        case OP_MUL:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_INTEGER_LITERAL, value * value2);
            break;

        case OP_DIV:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            assert(((int)value2 / (int)value) * (int)value == (int)value2); /* TODO: fraction */
            InterpreterPush(state, TYPE_INTEGER_LITERAL, (uint)((int)value2 / (int)value));
            break;

        case OP_REM:
            pop2(state, &type, &value, &type2, &value2);
            unbox(state, &type, &value);
            unbox(state, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            InterpreterPush(state, TYPE_INTEGER_LITERAL, value2 % value);
            break;

        case OP_CONCAT:
            pop2(state, &type, &value, &type2, &value2);
            size1 = InterpreterGetStringSize(state, type2, value2);
            size2 = InterpreterGetStringSize(state, type, value);
            if (!size1 && !size2)
            {
                InterpreterPush(state, TYPE_STRING_LITERAL,
                                (uint)StringPoolAdd(""));
                break;
            }
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

        case OP_INDEXED_ACCESS:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_OBJECT);
            if (!HeapCollectionGet(&state->heap, value2, &type, &value))
            {
                return;
            }
            InterpreterPush(state, type, value);
            break;

        case OP_RANGE:
            pop2(state, &type, &value, &type2, &value2);
            assert(type == TYPE_INTEGER_LITERAL);
            assert(type2 == TYPE_INTEGER_LITERAL);
            value = createRange(state, (int)value2, (int)value);
            if (!value)
            {
                return;
            }
            InterpreterPush(state, TYPE_OBJECT, value);
            break;

        case OP_ITER_INIT:
            pop(state, &type, &value);
            assert(type == TYPE_OBJECT);
            value = createIterator(state, value);
            if (!value)
            {
                return;
            }
            InterpreterPush(state, TYPE_OBJECT, value);
            break;

        case OP_ITER_NEXT:
            pop(state, &type, &value);
            assert(type == TYPE_OBJECT);
            assert(HeapGetObjectType(&state->heap, value) == TYPE_ITERATOR);
            assert(HeapGetObjectSize(&state->heap, value) == sizeof(Iterator));
            iter = (Iterator*)HeapGetObjectData(&state->heap, value);
            type = TYPE_NULL_LITERAL;
            value = 0;
            InterpreterPush(state, TYPE_BOOLEAN_LITERAL,
                            HeapIteratorNext(iter, &type, &value));
            InterpreterPush(state, type, value);
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            ip += jumpOffset;
            break;

        case OP_BRANCH_TRUE:
            assert(InterpreterPeekType(state) == TYPE_BOOLEAN_LITERAL);
            jumpOffset = BytecodeReadInt(&ip);
            if (popValue(state))
            {
                ip += jumpOffset;
            }
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
            baseIP = state->bytecode + FunctionIndexGetBytecodeOffset(FunctionIndexGetFunctionFromBytecode((uint)(ip - state->bytecode)));
            break;

        case OP_RETURN_VOID:
            if (!IntVectorSize(&state->callStack))
            {
                assert(IntVectorSize(&state->stack) ==
                       FunctionIndexGetLocalsCount(target));
                return;
            }
            popStackFrame(state, &ip, &bp, 0);
            baseIP = state->bytecode + FunctionIndexGetBytecodeOffset(FunctionIndexGetFunctionFromBytecode((uint)(ip - state->bytecode)));
            break;

        case OP_INVOKE:
            function = (functionref)BytecodeReadUint(&ip);
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == FunctionIndexGetParameterCount(function)); /* TODO */
            value = *ip++;
            pushStackFrame(state, &ip, &bp, function, value);
            baseIP = ip;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = (nativefunctionref)*ip++;
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == NativeGetParameterCount(nativeFunction)); /* TODO */
            state->error = NativeInvoke(state, nativeFunction, *ip++);
            if (state->error)
            {
                return;
            }
            break;

        case OP_PIPE_BEGIN:
            assert(!state->pipeOut);
            assert(!state->pipeErr);
            state->pipeOut = ByteVectorCreate();
            state->pipeErr = ByteVectorCreate();
            if (!state->pipeOut || !state->pipeErr)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            break;

        case OP_PIPE_END:
            assert(state->pipeOut);
            createString(state, state->pipeOut, &type, &value);
            storeLocal(state, bp, BytecodeReadUint16(&ip), type, value);
            ByteVectorDispose(state->pipeOut);
            free(state->pipeOut);
            state->pipeOut = null;

            assert(state->pipeErr);
            createString(state, state->pipeErr, &type, &value);
            storeLocal(state, bp, BytecodeReadUint16(&ip), type, value);
            ByteVectorDispose(state->pipeErr);
            free(state->pipeErr);
            state->pipeErr = null;

            if (state->error)
            {
                return;
            }
            break;
        }
    }
}

static void disposeState(RunState *state)
{
    HeapDispose(&state->heap);
    free(state->fields);
    IntVectorDispose(&state->callStack);
    ByteVectorDispose(&state->typeStack);
    IntVectorDispose(&state->stack);
}

static boolean handleError(RunState *state, ErrorCode error)
{
    state->error = error;
    if (error)
    {
        disposeState(state);
        return true;
    }
    return false;
}

ErrorCode InterpreterExecute(const byte *restrict bytecode, functionref target)
{
    RunState state;
    uint fieldCount = FieldIndexGetCount();

    memset(&state, 0, sizeof(state));
    state.bytecode = bytecode;
    state.fields = (uint*)malloc(fieldCount * (sizeof(int) +
                                               sizeof(ValueType)));
    if (handleError(&state, state.fields ? NO_ERROR : OUT_OF_MEMORY) ||
        handleError(&state, HeapInit(&state.heap)) ||
        handleError(&state, IntVectorInit(&state.callStack)) ||
        handleError(&state, ByteVectorInit(&state.typeStack)) ||
        handleError(&state, IntVectorInit(&state.stack)))
    {
        return state.error;
    }
    state.fieldTypes = (ValueType*)&state.fields[fieldCount];

    execute(&state, FunctionIndexGetFirstFunction());
    if (!state.error)
    {
        execute(&state, target);
    }

    disposeState(&state);

    return state.error;
}
