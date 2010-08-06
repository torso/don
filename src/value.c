#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "log.h"
#include "interpreterstate.h"
#include "value.h"
#include "collection.h"
#include "iterator.h"
#include "instruction.h"

#define VALUE_ENTRY_SIZE 2
#define VALUE_OFFSET_TYPE 0
#define VALUE_OFFSET_VALUE 1

typedef enum
{
    VALUE_UNEVALUATED,
    VALUE_COPY,
    VALUE_NULL,
    VALUE_BOOLEAN,
    VALUE_INTEGER,
    VALUE_STRING,
    VALUE_STACKFRAME,
    VALUE_OBJECT
} ValueType;


static boolean setError(RunState *state, ErrorCode error)
{
    state->error = error;
    return error ? true : false;
}

void ValueDump(const RunState *state)
{
    uint valueIndex;
    uint type;
    uint value;

    printf("Dump state, ip=%d bp=%d\n", state->ip, state->bp);
    for (valueIndex = 0; valueIndex < IntVectorSize(&state->values);
         valueIndex += VALUE_ENTRY_SIZE)
    {
        type = IntVectorGet(&state->values, valueIndex + VALUE_OFFSET_TYPE);
        value = IntVectorGet(&state->values, valueIndex + VALUE_OFFSET_VALUE);
        switch (type)
        {
        case VALUE_UNEVALUATED:
            printf(" %d: unevaluated dataip=%d\n", valueIndex, value);
            break;

        case VALUE_COPY:
            printf(" %d: copy value=%d\n", valueIndex, value);
            break;

        case VALUE_BOOLEAN:
            printf(" %d: boolean value=%d\n", valueIndex, value);
            break;

        case VALUE_INTEGER:
            printf(" %d: integer value=%d\n", valueIndex, value);
            break;

        case VALUE_STRING:
            printf(" %d: string %d: %s\n", valueIndex, value,
                   StringPoolGetString((stringref)value));
            break;

        case VALUE_STACKFRAME:
            printf(" %d: stackframe bp=%d\n", valueIndex, value);
            break;

        case VALUE_OBJECT:
            printf(" %d: object: offset=%d type=%d\n", valueIndex, value,
                   ByteVectorGet(&state->heap, value));
            break;

        default:
            printf(" %d: unknown %d:%d\n", valueIndex, type, value);
            assert(false);
            break;
        }
    }
}
static boolean addOverflow(int a, int b)
{
    return (boolean)(b < 1 ? MIN_INT - b > a : MAX_INT - b < a);
}

static boolean subOverflow(int a, int b)
{
    return (boolean)(b < 1 ? MAX_INT + b < a : MIN_INT + b > a);
}

static uint allocateObject(RunState *state, ObjectType type)
{
    uint offset = ByteVectorSize(&state->heap);
    setError(state, ByteVectorAdd(&state->heap, type));
    return offset;
}

uint ValueGetOffset(uint bp, uint value)
{
    return bp + value * VALUE_ENTRY_SIZE;
}

uint ValueGetRelativeOffset(const RunState *state, uint valueOffset,
                            uint bytecodeOffset)
{
    return valueOffset -
        ByteVectorGetPackUint(state->valueBytecode,
                              bytecodeOffset) * VALUE_ENTRY_SIZE;
}

static uint readRelativeValueOffset(const RunState *state, uint valueOffset,
                                    uint *bytecodeOffset)
{
    return valueOffset -
        ByteVectorReadPackUint(state->valueBytecode,
                               bytecodeOffset) * VALUE_ENTRY_SIZE;
}

static ValueType getValueType(const RunState *state, uint valueOffset)
{
    return IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_TYPE);
}

static ValueType getLocalValueType(const RunState *state, uint bp, uint value)
{
    return getValueType(state, ValueGetOffset(bp, value));
}

static uint getValue(const RunState *state, uint valueOffset)
{
    return IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_VALUE);
}

static uint getLocalValue(const RunState *state, uint bp, uint value)
{
    return getValue(state, ValueGetOffset(bp, value));
}

static void setValue(RunState *state, uint valueOffset, ValueType type,
                     uint value)
{
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_TYPE, type);
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_VALUE, value);
}

void ValueSetStackframeValue(RunState *state, uint valueOffset, uint stackframe)
{
    setValue(state, valueOffset, VALUE_STACKFRAME, stackframe);
}

static void evaluateValue(RunState *state, uint valueOffset)
{
    ValueType type;
    uint value = IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_VALUE);
    uint object;
    uint condition;
    uint value1;
    uint value2;
    int a;
    int b;

    switch (IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_TYPE))
    {
    case VALUE_UNEVALUATED:
        switch (ByteVectorRead(state->valueBytecode, &value))
        {
        case DATAOP_NULL:
            type = VALUE_NULL;
            value = 0;
            break;

        case DATAOP_TRUE:
            type = VALUE_BOOLEAN;
            value = true;
            break;

        case DATAOP_FALSE:
            type = VALUE_BOOLEAN;
            value = false;
            break;

        case DATAOP_INTEGER:
            type = VALUE_INTEGER;
            value = (uint)ByteVectorGetPackInt(state->valueBytecode, value);
            break;

        case DATAOP_STRING:
            type = VALUE_STRING;
            value = ByteVectorGetPackUint(state->valueBytecode, value);
            break;

        case DATAOP_LIST:
            type = VALUE_OBJECT;
            object = allocateObject(state, OBJECT_LIST);
            if (state->error ||
                (state->error = ByteVectorAddPackUint(&state->heap, value)) ||
                (state->error = ByteVectorAddPackUint(&state->heap,
                                                      valueOffset)))
            {
                return;
            }
            value = object;
            break;

        case DATAOP_CONDITION:
            condition = readRelativeValueOffset(state, valueOffset, &value);
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, condition);
            if (state->error)
            {
                return;
            }
            assert(getValueType(state, condition) == VALUE_BOOLEAN);
            value = getValue(state, condition) ? value2 : value1;
            evaluateValue(state, value);
            if (state->error)
            {
                return;
            }
            type = getValueType(state, value);
            value = getValue(state, value);
            break;

        case DATAOP_RETURN:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            assert(getValueType(state, value1) == VALUE_STACKFRAME);
            value2 = ValueGetOffset(
                getValue(state, value1),
                ByteVectorGetPackUint(state->valueBytecode, value));
            evaluateValue(state, value2);
            if (state->error)
            {
                return;
            }
            type = getValueType(state, value2);
            value = getValue(state, value2);
            break;

        case DATAOP_EQUALS:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, value1);
            if (state->error)
            {
                return;
            }
            evaluateValue(state, value2);
            if (state->error)
            {
                return;
            }
            type = VALUE_BOOLEAN;
            value =
                getValueType(state, value1) == getValueType(state, value2) &&
                getValue(state, value1) == getValue(state, value2);
            break;

        case DATAOP_ADD:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, value1);
            if (state->error)
            {
                return;
            }
            evaluateValue(state, value2);
            if (state->error)
            {
                return;
            }
            assert(getValueType(state, value1) == VALUE_INTEGER);
            assert(getValueType(state, value2) == VALUE_INTEGER);
            type = VALUE_INTEGER;
            a = (int)getValue(state, value1);
            b = (int)getValue(state, value2);
            /* TODO: Promote to bigger integer type */
            assert(!addOverflow(a, b));
            value = (uint)(a + b);
            break;

        case DATAOP_SUB:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, value1);
            if (state->error)
            {
                return;
            }
            evaluateValue(state, value2);
            if (state->error)
            {
                return;
            }
            assert(getValueType(state, value1) == VALUE_INTEGER);
            assert(getValueType(state, value2) == VALUE_INTEGER);
            type = VALUE_INTEGER;
            a = (int)getValue(state, value1);
            b = (int)getValue(state, value2);
            /* TODO: Promote to bigger integer type */
            assert(!subOverflow(a, b));
            value = (uint)(a - b);
            break;

        case DATAOP_INDEXED_ACCESS:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, value1);
            if (state->error)
            {
                return;
            }
            evaluateValue(state, value2);
            if (state->error)
            {
                return;
            }
            assert(getValueType(state, value1) == VALUE_OBJECT);
            assert(getValueType(state, value2) == VALUE_INTEGER);

            value = CollectionGetElementValueOffset(state,
                                                    getValue(state, value1),
                                                    getValue(state, value2));
            evaluateValue(state, value);
            if (state->error)
            {
                return;
            }
            type = getValueType(state, value);
            value = getValue(state, value);
            break;

        default:
            assert(false);
            return;
        }
        break;

    case VALUE_COPY:
        evaluateValue(state, value);
        if (state->error)
        {
            return;
        }
        type = IntVectorGet(&state->values, value + VALUE_OFFSET_TYPE);
        value = IntVectorGet(&state->values, value + VALUE_OFFSET_VALUE);
        break;

    default:
        return;
    }
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_TYPE, type);
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_VALUE, value);
}

static void copyValue(RunState *state, uint sourceBP, uint sourceValue,
                      uint destBP, uint destValue)
{
    ValueType type = getLocalValueType(state, sourceBP, sourceValue);
    uint value;

    if (type != VALUE_UNEVALUATED)
    {
        value = getLocalValue(state, sourceBP, sourceValue);
    }
    else
    {
        type = VALUE_COPY;
        value = ValueGetOffset(sourceBP, sourceValue);
    }
    setValue(state, ValueGetOffset(destBP, destValue), type, value);
}

boolean ValueGetBoolean(RunState *state, uint bp, uint valueIndex)
{
    evaluateValue(state, ValueGetOffset(bp, valueIndex));
    if (state->error)
    {
        return false;
    }
    assert(getLocalValueType(state, bp, valueIndex) == VALUE_BOOLEAN);
    return (boolean)getLocalValue(state, bp, valueIndex);
}

uint ValueCreateStackframe(RunState *state, uint ip, uint argumentCount)
{
    uint valueCount;
    uint value;
    uint argument;
    uint parentBP = state->bp;

    state->bp = IntVectorSize(&state->values);
    valueCount = ByteVectorReadPackUint(state->bytecode, &ip);
    for (value = 0; value < valueCount; value++)
    {
        if (setError(state, IntVectorAdd(&state->values, VALUE_UNEVALUATED)) ||
            setError(state, IntVectorAdd(
                         &state->values,
                         ByteVectorReadPackUint(state->bytecode, &ip))))
        {
            return 0;
        }
    }

    for (argument = 0; argument < argumentCount; argument++)
    {
        copyValue(state,
                  parentBP, ByteVectorReadPackUint(state->bytecode, &state->ip),
                  state->bp, argument);
    }

    if (setError(state, IntVectorAdd(&state->stack, state->ip)) ||
        setError(state, IntVectorAdd(&state->stack, parentBP)))
    {
        setError(state, OUT_OF_MEMORY);
        return 0;
    }

    state->ip = ip;

    return state->bp;
}

void ValueDestroyStackframe(RunState *state)
{
    state->bp = IntVectorPop(&state->stack);
    state->ip = IntVectorPop(&state->stack);
}

static void printObject(RunState *state, uint object)
{
    Iterator iterator;
    boolean first = true;

    IteratorInit(&iterator, state, object);
    printf("[");
    while (IteratorHasNext(&iterator))
    {
        if (!first)
        {
            printf(" ");
        }
        first = false;
        IteratorNext(&iterator);
        ValuePrint(state, IteratorGetValueOffset(&iterator));
    }
    printf("]");
}

void ValuePrint(RunState *state, uint valueOffset)
{
    uint value;

    evaluateValue(state, valueOffset);
    if (state->error)
    {
        return;
    }
    value = getValue(state, valueOffset);
    switch (getValueType(state, valueOffset))
    {
    case VALUE_NULL:
        printf("null");
        break;

    case VALUE_BOOLEAN:
        printf(value ? "true" : "false");
        break;

    case VALUE_INTEGER:
        printf("%d", value);
        break;

    case VALUE_STRING:
        printf("%s", StringPoolGetString((stringref)value));
        break;

    case VALUE_OBJECT:
        printObject(state, value);
        break;

    case VALUE_UNEVALUATED:
    case VALUE_COPY:
    case VALUE_STACKFRAME:
    default:
        assert(false);
        break;
    }
}
