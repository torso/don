#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "interpreter.h"
#include "instruction.h"
#include "native.h"

static const boolean DUMP_STATE = false;
static const boolean TRACE = false;

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
    VALUE_STACKFRAME
} ValueType;

typedef struct
{
    const bytevector *restrict bytecode;
    const bytevector *restrict valueBytecode;

    uint ip;
    uint bp;
    intvector values;
    intvector stack;
} State;


static void dumpState(const State *state)
{
    uint valueIndex;
    uint type;
    uint value;

    printf("Dump state, ip=%d bp=%d\n", state->ip, state->bp);
    for (valueIndex = 0; valueIndex < IntVectorSize(&state->values); valueIndex += VALUE_ENTRY_SIZE)
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

        case VALUE_STRING:
            printf(" %d: string %d: %s\n", valueIndex, value,
                   StringPoolGetString((stringref)value));
            break;

        case VALUE_STACKFRAME:
            printf(" %d: stackframe bp=%d\n", valueIndex, value);
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

static uint getValueOffset(uint bp, uint value)
{
    return bp + value * VALUE_ENTRY_SIZE;
}

static ValueType getValueType(const State *state, uint valueOffset)
{
    return IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_TYPE);
}

static ValueType getLocalValueType(const State *state, uint bp, uint value)
{
    return getValueType(state, getValueOffset(bp, value));
}

static uint getValue(const State *state, uint valueOffset)
{
    return IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_VALUE);
}

static uint getLocalValue(const State *state, uint bp, uint value)
{
    return getValue(state, getValueOffset(bp, value));
}

static void setValue(State *state, uint bp, uint valueIndex,
                     ValueType type, uint value)
{
    uint offset = getValueOffset(bp, valueIndex);
    IntVectorSet(&state->values, offset + VALUE_OFFSET_TYPE, type);
    IntVectorSet(&state->values, offset + VALUE_OFFSET_VALUE, value);
}

static uint readRelativeValueOffset(State *state, uint valueOffset,
                                    uint *bytecodeOffset)
{
    return valueOffset -
        ByteVectorReadPackUint(state->valueBytecode,
                               bytecodeOffset) * VALUE_ENTRY_SIZE;
}

static void evaluateValue(State *state, uint valueOffset)
{
    ValueType type;
    uint value = IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_VALUE);
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

        case DATAOP_PHI_VARIABLE:
            condition = readRelativeValueOffset(state, valueOffset, &value);
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, condition);
            assert(getValueType(state, condition) == VALUE_BOOLEAN);
            value = getValue(state, condition) ? value2 : value1;
            evaluateValue(state, value);
            type = getValueType(state, value);
            value = getValue(state, value);
            break;

        case DATAOP_RETURN:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            assert(getValueType(state, value1) == VALUE_STACKFRAME);
            value2 = getValueOffset(
                getValue(state, value1),
                ByteVectorGetPackUint(state->valueBytecode, value));
            evaluateValue(state, value2);
            type = getValueType(state, value2);
            value = getValue(state, value2);
            break;

        case DATAOP_EQUALS:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, value1);
            evaluateValue(state, value2);
            type = VALUE_BOOLEAN;
            value =
                getValueType(state, value1) == getValueType(state, value2) &&
                getValue(state, value1) == getValue(state, value2);
            break;

        case DATAOP_ADD:
            value1 = readRelativeValueOffset(state, valueOffset, &value);
            value2 = readRelativeValueOffset(state, valueOffset, &value);
            evaluateValue(state, value1);
            evaluateValue(state, value2);
            assert(getValueType(state, value1) == VALUE_INTEGER);
            assert(getValueType(state, value2) == VALUE_INTEGER);
            type = VALUE_INTEGER;
            a = (int)getValue(state, value1);
            b = (int)getValue(state, value2);
            /* TODO: Promote to bigger integer type */
            assert(!addOverflow(a, b));
            value = (uint)(a + b);
            break;

        default:
            assert(false);
            return;
        }
        break;

    case VALUE_COPY:
        evaluateValue(state, value);
        type = IntVectorGet(&state->values, value + VALUE_OFFSET_TYPE);
        value = IntVectorGet(&state->values, value + VALUE_OFFSET_VALUE);
        break;

    default:
        return;
    }
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_TYPE, type);
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_VALUE, value);
}

static void copyValue(State *state, uint sourceBP, uint sourceValue,
                      uint destBP, uint destValue)
{
    ValueType type = getLocalValueType(state, sourceBP, sourceValue);

    if (type != VALUE_UNEVALUATED)
    {
        setValue(state, destBP, destValue,
                 getLocalValueType(state, sourceBP, sourceValue),
                 getLocalValue(state, sourceBP, sourceValue));
    }
    else
    {
        setValue(state, destBP, destValue,
                 VALUE_COPY, getValueOffset(sourceBP, sourceValue));
    }
}

static boolean getBooleanValue(State *state, uint bp, uint valueIndex)
{
    evaluateValue(state, getValueOffset(bp, valueIndex));
    assert(getLocalValueType(state, bp, valueIndex) == VALUE_BOOLEAN);
    return (boolean)getLocalValue(state, bp, valueIndex);
}

static void createStackframe(State *state, uint ip, uint argumentCount)
{
    uint valueCount;
    uint value;
    uint argument;
    uint parentBP = state->bp;

    state->bp = IntVectorSize(&state->values);
    valueCount = ByteVectorReadPackUint(state->bytecode, &ip);
    for (value = 0; value < valueCount; value++)
    {
        IntVectorAdd(&state->values, VALUE_UNEVALUATED);
        IntVectorAdd(&state->values,
                     ByteVectorReadPackUint(state->bytecode, &ip));
    }

    for (argument = 0; argument < argumentCount; argument++)
    {
        copyValue(state,
                  parentBP, ByteVectorReadPackUint(state->bytecode, &state->ip),
                  state->bp, argument);
    }

    IntVectorAdd(&state->stack, state->ip);
    IntVectorAdd(&state->stack, parentBP);

    state->ip = ip;
}

static void destroyStackframe(State *state)
{
    state->bp = IntVectorPop(&state->stack);
    state->ip = IntVectorPop(&state->stack);
}

static void invokeNative(State* state, nativefunctionref function)
{
    uint value;

    if (function == 0)
    {
        evaluateValue(state, getValueOffset(state->bp, 0));
        value = getLocalValue(state, state->bp, 0);
        switch (getLocalValueType(state, state->bp, 0))
        {
        case VALUE_NULL:
            printf("null\n");
            break;

        case VALUE_BOOLEAN:
            printf(value ? "true\n" : "false\n");
            break;

        case VALUE_INTEGER:
            printf("%d\n", value);
            break;

        case VALUE_STRING:
            printf("%s\n", StringPoolGetString((stringref)value));
            break;

        case VALUE_UNEVALUATED:
        case VALUE_COPY:
        case VALUE_STACKFRAME:
        default:
            assert(false);
            break;
        }
    }
}

static void execute(State *state)
{
    uint condition;
    uint offset;
    nativefunctionref nativeFunction;
    uint function;
    uint argumentCount;

    createStackframe(state, state->ip, 0);

    /* Remove old (non-existing) stackframe pushed onto the stack by
     * createStackframe. */
    IntVectorSetSize(&state->stack, 0);

    for (;;)
    {
        if (TRACE)
        {
            printf("execute ip=%d bp=%d stacksize=%d\n", state->ip, state->bp, IntVectorSize(&state->stack));
        }
        switch (ByteVectorRead(state->bytecode, &state->ip))
        {
        case OP_RETURN:
            if (!IntVectorSize(&state->stack))
            {
                return;
            }
            destroyStackframe(state);
            break;

        case OP_BRANCH:
            condition = ByteVectorReadPackUint(state->bytecode, &state->ip);
            offset = ByteVectorReadPackUint(state->bytecode, &state->ip);
            if (!getBooleanValue(state, state->bp, condition))
            {
                state->ip += offset;
            }
            break;

        case OP_JUMP:
            offset = ByteVectorReadPackUint(state->bytecode, &state->ip);
            state->ip += offset;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = (nativefunctionref)ByteVectorRead(state->bytecode,
                                                               &state->ip);
            setValue(state, state->bp,
                     ByteVectorReadPackUint(state->bytecode, &state->ip),
                     VALUE_STACKFRAME, IntVectorSize(&state->values));
            createStackframe(
                state, NativeGetBytecodeOffset(nativeFunction),
                ByteVectorReadPackUint(state->bytecode, &state->ip));
            invokeNative(state, nativeFunction);
            destroyStackframe(state);
            break;

        case OP_COND_INVOKE:
            condition = ByteVectorReadPackUint(state->bytecode, &state->ip);
            function = ByteVectorReadPackUint(state->bytecode, &state->ip);
            if (getBooleanValue(state, state->bp, condition))
            {
                setValue(state, state->bp,
                         ByteVectorReadPackUint(state->bytecode, &state->ip),
                         VALUE_STACKFRAME, IntVectorSize(&state->values));
                createStackframe(
                    state, function,
                    ByteVectorReadPackUint(state->bytecode, &state->ip));
            }
            else
            {
                ByteVectorSkipPackUint(state->bytecode, &state->ip);
                argumentCount = ByteVectorReadPackUint(state->bytecode,
                                                       &state->ip);
                while (argumentCount--)
                {
                    ByteVectorSkipPackUint(state->bytecode, &state->ip);
                }
            }
            break;

        default:
            assert(false);
            break;
        }
    }
}

void InterpreterExecute(const bytevector *restrict bytecode,
                        const bytevector *restrict valueBytecode,
                        targetref target)
{
    State state;

    state.ip = TargetIndexGetBytecodeOffset(target);
    state.bp = 0;
    state.bytecode = bytecode;
    state.valueBytecode = valueBytecode;
    IntVectorInit(&state.values);
    IntVectorInit(&state.stack);

    execute(&state);
    if (DUMP_STATE)
    {
        dumpState(&state);
    }

    IntVectorDispose(&state.values);
    IntVectorDispose(&state.stack);
}
