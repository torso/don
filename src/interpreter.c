#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "interpreter.h"
#include "instruction.h"
#include "native.h"

#define VALUE_ENTRY_SIZE 2
#define VALUE_OFFSET_TYPE 0
#define VALUE_OFFSET_VALUE 1

#define VALUE_UNEVALUATED 0
#define VALUE_COPY 1
#define VALUE_BOOLEAN 2
#define VALUE_STRING 3
#define VALUE_STACKFRAME 4

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
#ifdef DEBUG
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
#endif
}

static uint getValueOffset(uint bp, uint value)
{
    return bp + value * VALUE_ENTRY_SIZE;
}

static uint getValueType(const State *state, uint bp, uint value)
{
    return IntVectorGet(&state->values,
                        getValueOffset(bp, value) + VALUE_OFFSET_TYPE);
}

static uint getValue(const State *state, uint bp, uint value)
{
    return IntVectorGet(&state->values,
                        getValueOffset(bp, value) + VALUE_OFFSET_VALUE);
}

static void setValue(State *state, uint bp, uint valueIndex,
                     uint type, uint value)
{
    uint offset = getValueOffset(bp, valueIndex);
    IntVectorSet(&state->values, offset + VALUE_OFFSET_TYPE, type);
    IntVectorSet(&state->values, offset + VALUE_OFFSET_VALUE, value);
}

static void evaluateValue(State *state, uint valueOffset)
{
    uint type;
    uint value = IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_VALUE);
    uint condition;
    uint value1;
    uint value2;

    printf("Evaluate %d\n", valueOffset);
    switch (IntVectorGet(&state->values, valueOffset + VALUE_OFFSET_TYPE))
    {
    case VALUE_UNEVALUATED:
        printf(" Evaluate ip=%d op=%d\n", value, ByteVectorGet(state->valueBytecode, value));
        switch (ByteVectorRead(state->valueBytecode, &value))
        {
        case DATAOP_TRUE:
            type = VALUE_BOOLEAN;
            value = true;
            break;

        case DATAOP_FALSE:
            type = VALUE_BOOLEAN;
            value = false;
            break;

        case DATAOP_STRING:
            type = VALUE_STRING;
            value = ByteVectorGetPackUint(state->valueBytecode, value);
            break;

        case DATAOP_PHI_VARIABLE:
            condition = ByteVectorReadPackUint(state->valueBytecode, &value);
            value1 = ByteVectorReadPackUint(state->valueBytecode, &value);
            value2 = ByteVectorGetPackUint(state->valueBytecode, value);
            evaluateValue(state, valueOffset - condition * VALUE_ENTRY_SIZE);
            printf("condition: %d %d %d %d\n", valueOffset, condition, valueOffset - condition * VALUE_ENTRY_SIZE, IntVectorGet(&state->values, valueOffset - condition * VALUE_ENTRY_SIZE));
            assert(IntVectorGet(&state->values,
                                valueOffset - condition * VALUE_ENTRY_SIZE +
                                VALUE_OFFSET_TYPE) ==
                   VALUE_BOOLEAN);
            value = IntVectorGet(&state->values,
                                 valueOffset - condition * VALUE_ENTRY_SIZE +
                                 VALUE_OFFSET_VALUE) ? value2 : value1;
            evaluateValue(state, valueOffset - value * VALUE_ENTRY_SIZE);
            type = IntVectorGet(
                &state->values,
                valueOffset - value * VALUE_ENTRY_SIZE + VALUE_OFFSET_TYPE);
            value = IntVectorGet(
                &state->values,
                valueOffset - value * VALUE_ENTRY_SIZE + VALUE_OFFSET_VALUE);
            break;

        default:
            assert(false);
            break;
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
    printf("evaluate %d, result: %d:%d\n", valueOffset, type, value);
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_TYPE, type);
    IntVectorSet(&state->values, valueOffset + VALUE_OFFSET_VALUE, value);
}

static void copyValue(State *state, uint sourceBP, uint sourceValue,
                      uint destBP, uint destValue)
{
    uint type = getValueType(state, sourceBP, sourceValue);

    printf("Copying value %d:%d to %d:%d value: %d:%d\n", sourceBP, sourceValue,
           destBP, destValue, getValueType(state, sourceBP, sourceValue),
           getValue(state, sourceBP, sourceValue));
    if (type != VALUE_UNEVALUATED)
    {
        setValue(state, destBP, destValue,
                 getValueType(state, sourceBP, sourceValue),
                 getValue(state, sourceBP, sourceValue));
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
    assert(getValueType(state, bp, valueIndex) == VALUE_BOOLEAN);
    return (boolean)getValue(state, bp, valueIndex);
}

static void createStackframe(State *state, uint ip, uint argumentCount)
{
    uint valueCount;
    uint value;
    uint argument;
    uint parentBP = state->bp;

    state->bp = IntVectorSize(&state->values);
    printf("Creating stackframe for function ip=%d bp=%d values=%d arguments=%d\n", ip, state->bp, ByteVectorGetPackUint(state->bytecode, ip), argumentCount);
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
    dumpState(state);
    if (function == 0)
    {
        evaluateValue(state, getValueOffset(state->bp, 0));
        assert(getValueType(state, state->bp, 0) == VALUE_STRING);
        printf("%s\n", StringPoolGetString((stringref)getValue(state, state->bp, 0)));
    }
    dumpState(state);
}

static void execute(State *state)
{
    uint condition;
    uint offset;
    nativefunctionref function;

    createStackframe(state, state->ip, 0);

    for (;;)
    {
        printf("execute, ip=%d bp=%d\n", state->ip, state->bp);
        switch (ByteVectorRead(state->bytecode, &state->ip))
        {
        case OP_RETURN:
            destroyStackframe(state);
            return;

        case OP_BRANCH:
            condition = ByteVectorReadPackUint(state->bytecode, &state->ip);
            offset = ByteVectorReadPackUint(state->bytecode, &state->ip);
            if (!getBooleanValue(state, state->bp, condition))
            {
                state->ip += offset;
            }
            break;

        case OP_INVOKE_NATIVE:
            function = (nativefunctionref)ByteVectorRead(state->bytecode,
                                                         &state->ip);
            setValue(state, state->bp,
                     ByteVectorReadPackUint(state->bytecode, &state->ip),
                     VALUE_STACKFRAME, IntVectorSize(&state->values));
            createStackframe(
                state, NativeGetBytecodeOffset(function),
                ByteVectorReadPackUint(state->bytecode, &state->ip));
            invokeNative(state, function);
            destroyStackframe(state);
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
    printf("Execute target=%d offset=%d\n", target, state.ip);

    execute(&state);

    IntVectorFree(&state.values);
    IntVectorFree(&state.stack);
}
