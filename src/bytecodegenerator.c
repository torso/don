#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "instruction.h"
#include "interpreterstate.h"
#include "native.h"
#include "bytecodegenerator.h"

static const boolean DUMP_PARSED   = false;
static const boolean DUMP_BYTECODE = false;
static const boolean DUMP_STATE    = false;

typedef struct
{
    bytevector *parsed;
    intvector data;
    ErrorCode error;
} State;

static const uint OFFSET_PARSED_OFFSET = 0;
static const uint OFFSET_BYTECODE_OFFSET = 1;
static const uint OFFSET_VALUES = 2;
static const uint OFFSET_ENTRY_SIZE = 2;

static const uint VALUE_ENTRY_SIZE = 2;
static const uint OFFSET_VALUE_OFFSET = 0;
static const uint OFFSET_VALUE_NEWINDEX = 1;

static const uint VALUE_UNUSED = (uint)-2;
static const uint VALUE_USED_UNALLOCATED = (uint)-1;


static boolean setError(State *state, ErrorCode error)
{
    state->error = error;
    return error ? true : false;
}

static void dumpParsed(const bytevector *parsed)
{
    uint ip;
    uint readIndex;
    uint dataStart;
    uint controlStart;
    uint dataSize;
    uint controlSize;
    uint valueCount;
    uint stop;
    uint i;
    uint function;
    uint argumentCount;
    uint condition;
    uint value;
    uint target;
    uint stackframe;
    stringref name;

    printf("Dump parsed\n");
    for (readIndex = 0; readIndex < ByteVectorSize(parsed);)
    {
        function = readIndex;
        readIndex += (uint)sizeof(uint);
        valueCount = ByteVectorReadPackUint(parsed, &readIndex);
        dataSize = ByteVectorReadPackUint(parsed, &readIndex);
        controlSize = ByteVectorReadPackUint(parsed, &readIndex);

        dataStart = readIndex;
        controlStart = readIndex + dataSize;

        printf("function=%d: data, count=%d size=%d\n",
               function, valueCount, dataSize);
        for (readIndex = dataStart, stop = dataStart + dataSize, ip = 0;
             readIndex < stop; ip++)
        {
            switch (ByteVectorRead(parsed, &readIndex))
            {
            case DATAOP_NULL:
                printf("%d: null\n", ip);
                break;

            case DATAOP_TRUE:
                printf("%d: true\n", ip);
                break;

            case DATAOP_FALSE:
                printf("%d: false\n", ip);
                break;

            case DATAOP_INTEGER:
                printf("%d: integer %d\n", ip,
                       ByteVectorReadPackInt(parsed, &readIndex));
                break;

            case DATAOP_STRING:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: string %d:\"%s\"\n", ip, value,
                       StringPoolGetString((stringref)value));
                break;

            case DATAOP_LIST:
                valueCount = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: list length=%d [", ip, valueCount);
                while (valueCount--)
                {
                    printf(valueCount ? "%d " : "%d", ByteVectorReadPackUint(parsed, &readIndex));
                }
                printf("]\n");
                break;

            case DATAOP_CONDITION:
                condition = ByteVectorReadUint(parsed, &readIndex);
                value = ByteVectorReadUint(parsed, &readIndex);
                printf("%d: condition: %d %d %d\n", ip, condition,
                       value, ByteVectorReadInt(parsed, &readIndex));
                break;

            case DATAOP_PARAMETER:
                name = (stringref)ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: parameter name=%s\n", ip, StringPoolGetString(name));
                break;

            case DATAOP_RETURN:
                stackframe = ByteVectorReadPackUint(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: return %d from %d\n", ip, value, stackframe);
                break;

            case DATAOP_STACKFRAME:
                printf("%d: stackframe\n", ip);
                break;

            case DATAOP_STACKFRAME_ABSOLUTE:
                function = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: stackframe function=%d\n", ip, function);
                break;

            case DATAOP_EQUALS:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: equals %d %d\n", ip,
                       value, ByteVectorReadPackUint(parsed, &readIndex));
                break;

            case DATAOP_ADD:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: add %d %d\n", ip,
                       value, ByteVectorReadPackUint(parsed, &readIndex));
                break;

            case DATAOP_SUB:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: sub %d %d\n", ip,
                       value, ByteVectorReadPackUint(parsed, &readIndex));
                break;

            case DATAOP_INDEXED_ACCESS:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: indexed access %d[%d]\n", ip,
                       value, ByteVectorReadPackUint(parsed, &readIndex));
                break;

            default:
                assert(false);
                break;
            }
        }

        printf("control, size=%d\n", controlSize);
        for (readIndex = controlStart, stop = controlStart + controlSize;
             readIndex < stop;)
        {
            ip = readIndex;
            switch (ByteVectorRead(parsed, &readIndex))
            {
            case OP_RETURN:
                printf("%d: return\n", ip);
                break;

            case OP_BRANCH:
                condition = ByteVectorReadPackUint(parsed, &readIndex);
                target = ByteVectorReadUint(parsed, &readIndex);
                printf("%d: branch condition=%d target=%d\n",
                       ip, condition, readIndex + target);
                break;

            case OP_JUMP:
                target = ByteVectorReadUint(parsed, &readIndex);
                printf("%d: jump %d\n", ip, readIndex + target);
                break;

            case OP_INVOKE_NATIVE:
                function = ByteVectorRead(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
                argumentCount = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: invoke native function=%d, arguments=%d, stackframe=%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(parsed, &readIndex));
                }
                break;

            case OP_INVOKE_TARGET:
                function = ByteVectorReadPackUint(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
                argumentCount = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: invoke target=%d, arguments=%d, stackframe=%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(parsed, &readIndex));
                }
                break;

            case OP_COND_INVOKE:
                condition = ByteVectorReadPackUint(parsed, &readIndex);
                value = ByteVectorReadPackUint(parsed, &readIndex);
                argumentCount = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: cond_invoke condition=%d, arguments=%d, stackframe=%d\n",
                       ip, condition, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(parsed, &readIndex));
                }
                break;

            default:
                assert(false);
                break;
            }
        }
    }
}

static void dumpValueBytecode(const bytevector *bytecode)
{
    uint readIndex;
    uint valueOffset;
    uint condition;
    uint value;
    uint valueCount;
    uint stackframe;

    for (readIndex = 0; readIndex < ByteVectorSize(bytecode);)
    {
        valueOffset = readIndex;
        switch (ByteVectorRead(bytecode, &readIndex))
        {
        case DATAOP_NULL:
            printf("vb%d: null\n", valueOffset);
            break;

        case DATAOP_TRUE:
            printf("vb%d: true\n", valueOffset);
            break;

        case DATAOP_FALSE:
            printf("vb%d: false\n", valueOffset);
            break;

        case DATAOP_INTEGER:
            printf("vb%d: integer %d\n", valueOffset,
                   ByteVectorReadPackInt(bytecode, &readIndex));
            break;

        case DATAOP_STRING:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: string %d: \"%s\"\n", valueOffset,
                   value, StringPoolGetString((stringref)value));
            break;

        case DATAOP_LIST:
            valueCount = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: list length=%d [", valueOffset, valueCount);
            while (valueCount--)
            {
                printf(valueCount ? "-%d " : "-%d", ByteVectorReadPackUint(bytecode, &readIndex));
            }
            printf("]\n");
            break;

        case DATAOP_CONDITION:
            condition = ByteVectorReadPackUint(bytecode, &readIndex);
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: condition: -%d -%d -%d\n",
                   valueOffset, condition, value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;

        case DATAOP_PARAMETER:
            printf("vb%d: parameter name=%s\n", valueOffset,
                   StringPoolGetString((stringref)ByteVectorReadPackUint(
                                           bytecode, &readIndex)));
            break;

        case DATAOP_RETURN:
            stackframe = ByteVectorReadPackUint(bytecode, &readIndex);
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: return %d from -%d\n", valueOffset, value, stackframe);
            break;

        case DATAOP_STACKFRAME:
            printf("vb%d: stackframe\n", valueOffset);
            break;

        case DATAOP_EQUALS:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: equals -%d -%d\n", valueOffset, value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;

        case DATAOP_ADD:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: add -%d -%d\n", valueOffset, value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;

        case DATAOP_SUB:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: sub -%d -%d\n", valueOffset, value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;

        case DATAOP_INDEXED_ACCESS:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("vb%d: indexed access -%d[-%d]\n", valueOffset,
                   value, ByteVectorReadPackUint(bytecode, &readIndex));
            break;

        default:
            printf("vb%d: %d\n", valueOffset, ByteVectorGet(bytecode, readIndex - 1));
            assert(false);
            break;
        }
    }
}

static void dumpBytecode(const bytevector *bytecode)
{
    uint ip;
    uint readIndex;
    byte op;
    uint valueCount;
    uint i;
    uint function;
    uint argumentCount;
    uint condition;
    uint value;
    uint target;
    nativefunctionref nativeFunction;

    for (readIndex = 0; readIndex < ByteVectorSize(bytecode);)
    {
        function = readIndex;
        valueCount = ByteVectorReadPackUint(bytecode, &readIndex);
        target = TargetIndexGetTargetFromBytecode(function);
        if (target)
        {
            printf("function %d at b%d \"%s\", value count=%d\n", target, function, StringPoolGetString(TargetIndexGetName(target)), valueCount);
        }
        else
        {
            nativeFunction = NativeGetFromBytecodeOffset(function);
            printf("native function %d at b%d \"%s\", value count=%d\n", nativeFunction, function, StringPoolGetString(NativeGetName(nativeFunction)), valueCount);
        }
        for (value = 0; value < valueCount; value++)
        {
            printf(" value %d at vb%d\n", value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
        }
        for (;;)
        {
            ip = readIndex;
            op = ByteVectorRead(bytecode, &readIndex);
            if (op == (byte)-1)
            {
                break;
            }
            switch (op)
            {
            case OP_RETURN:
                printf(" b%d: return\n", ip);
                break;

            case OP_BRANCH:
                condition = ByteVectorReadPackUint(bytecode, &readIndex);
                target = ByteVectorReadPackUint(bytecode, &readIndex);
                printf(" b%d: branch condition=v%d jump=b%d\n",
                       ip, condition, readIndex + target);
                break;

            case OP_JUMP:
                target = ByteVectorReadPackUint(bytecode, &readIndex);
                printf(" b%d: jump b%d\n", ip, readIndex + target);
                break;

            case OP_INVOKE_NATIVE:
                function = ByteVectorRead(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf(" b%d: invoke native function=%d, arguments=%d, stackframe=v%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument v%d\n", i,
                           ByteVectorReadPackUint(bytecode, &readIndex));
                }
                break;

            case OP_INVOKE_TARGET:
                function = ByteVectorReadPackUint(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf(" b%d: invoke target=%d, arguments=%d, stackframe=v%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument v%d\n", i,
                           ByteVectorReadPackUint(bytecode, &readIndex));
                }
                break;

            case OP_COND_INVOKE:
                condition = ByteVectorReadPackUint(bytecode, &readIndex);
                function = ByteVectorReadPackUint(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf(" b%d: cond_invoke function=%d condition=v%d, arguments=%d, stackframe=v%d\n",
                       ip, function, condition, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument v%d\n", i,
                           ByteVectorReadPackUint(bytecode, &readIndex));
                }
                break;

            default:
                printf(" b%d: %d\n", ip, op);
                assert(false);
                break;
            }
        }
    }
}

static uint getParsedOffset(const State *state, uint dataOffset)
{
    return IntVectorGet(&state->data, dataOffset + OFFSET_PARSED_OFFSET);
}

static uint getBytecodeOffset(const State *state, uint dataOffset)
{
    return IntVectorGet(&state->data, dataOffset + OFFSET_BYTECODE_OFFSET);
}

static void setBytecodeOffset(State *state, uint dataOffset,
                              uint bytecodeOffset)
{
    IntVectorSet(&state->data, dataOffset + OFFSET_BYTECODE_OFFSET,
                 bytecodeOffset);
}

static uint getDataOffset(const State *state, uint parsedOffset)
{
    return ByteVectorGetUint(state->parsed, parsedOffset);
}

static uint getValueCount(const State *state, uint dataOffset)
{
    return ByteVectorGetPackUint(
        state->parsed, getParsedOffset(state, dataOffset) + (uint)sizeof(uint));
}

static void checkValueIndex(const State *state, uint dataOffset, uint value)
{
    assert(value < getValueCount(state, dataOffset));
}

static uint getValueOffset(const State *state, uint dataOffset, uint value)
{
    checkValueIndex(state, dataOffset, value);
    return IntVectorGet(&state->data,
                        dataOffset + value * VALUE_ENTRY_SIZE +
                        OFFSET_VALUES + OFFSET_VALUE_OFFSET);
}

static uint getValueInstruction(const State *state, uint dataOffset, uint value)
{
    return ByteVectorGet(state->parsed,
                         getValueOffset(state, dataOffset, value));
}

static uint getNewIndex(const State *state, uint dataOffset, uint value)
{
    checkValueIndex(state, dataOffset, value);
    return IntVectorGet(&state->data,
                        dataOffset + value * VALUE_ENTRY_SIZE +
                        OFFSET_VALUES + OFFSET_VALUE_NEWINDEX);
}

static uint getAllocatedNewIndex(const State *state, uint dataOffset,
                                 uint value)
{
    uint index = getNewIndex(state, dataOffset, value);
    assert(index != VALUE_UNUSED);
    assert(index != VALUE_USED_UNALLOCATED);
    return index;
}

static void setNewIndex(State *state, uint dataOffset, uint value,
                        uint newIndex)
{
    checkValueIndex(state, dataOffset, value);
    IntVectorSet(&state->data,
                 dataOffset + value * VALUE_ENTRY_SIZE +
                 OFFSET_VALUES + OFFSET_VALUE_NEWINDEX,
                 newIndex);
}

static boolean isUsed(const State *state, uint dataOffset, uint value)
{
    checkValueIndex(state, dataOffset, value);
    return getNewIndex(state, dataOffset, value) != VALUE_UNUSED;
}

static void useValue(State *state, uint dataOffset, uint value)
{
    uint readIndex;
    uint stackframe;
    uint stackframeOffset;
    uint function;
    uint valueCount;

    checkValueIndex(state, dataOffset, value);
    if (!isUsed(state, dataOffset, value))
    {
        setNewIndex(state, dataOffset, value, VALUE_USED_UNALLOCATED);
        readIndex = getValueOffset(state, dataOffset, value);
        switch (ByteVectorGet(state->parsed, readIndex++))
        {
        case DATAOP_LIST:
            valueCount = ByteVectorReadPackUint(state->parsed, &readIndex);
            while (valueCount-- > 0)
            {
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
            }
            break;

        case DATAOP_CONDITION:
            useValue(state, dataOffset, ByteVectorReadUint(state->parsed, &readIndex));
            useValue(state, dataOffset, ByteVectorReadUint(state->parsed, &readIndex));
            useValue(state, dataOffset, ByteVectorReadUint(state->parsed, &readIndex));
            break;

        case DATAOP_RETURN:
            stackframe = ByteVectorReadPackUint(state->parsed, &readIndex);
            assert(isUsed(state, dataOffset, stackframe));
            stackframeOffset = getValueOffset(state, dataOffset, stackframe);
            if (ByteVectorGet(state->parsed, stackframeOffset) ==
                DATAOP_STACKFRAME_ABSOLUTE)
            {
                value = ByteVectorReadPackUint(state->parsed, &readIndex);
                function = ByteVectorGetPackUint(state->parsed, stackframeOffset + 1);
                useValue(state, getDataOffset(state, function), value);
            }
            break;

        case DATAOP_EQUALS:
        case DATAOP_ADD:
        case DATAOP_SUB:
        case DATAOP_INDEXED_ACCESS:
            useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
            useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
            break;
        }
    }
}

static void dumpState(const State *state)
{
    uint dataOffset = 0;
    uint valueCount;
    uint value;

    while (dataOffset < IntVectorSize(&state->data))
    {
        printf("Function %d parsed at %d\n",
               dataOffset, getParsedOffset(state, dataOffset));
        valueCount = getValueCount(state, dataOffset);
        for (value = 0; value < valueCount; value++)
        {
            printf("Value %d at %d new %d\n", value,
                   getValueOffset(state, dataOffset, value),
                   getNewIndex(state, dataOffset, value));
        }
        dataOffset += OFFSET_ENTRY_SIZE + valueCount * VALUE_ENTRY_SIZE;
    }
}

static void markArguments(State *restrict state, uint dataOffset,
                          uint *restrict readIndex)
{
    uint argumentCount;

    for (argumentCount = ByteVectorReadPackUint(state->parsed, readIndex);
         argumentCount > 0;
         argumentCount--)
    {
        useValue(state, dataOffset,
                 ByteVectorReadPackUint(state->parsed, readIndex));
    }
}

static void markUsedValues(State *state)
{
    uint readIndex;
    uint stop;
    uint dataOffset;
    uint dataSize;
    uint controlSize;
    uint valueCount;
    uint value;

    for (readIndex = 0; readIndex < ByteVectorSize(state->parsed);)
    {
        dataOffset = IntVectorSize(&state->data);
        if (setError(state, IntVectorAdd(&state->data, readIndex)) || /* PARSED_OFFSET */
            setError(state, IntVectorAdd(&state->data, 0))) /* BYTECODE_OFFSET */
        {
            return;
        }

        ByteVectorWriteUint(state->parsed, &readIndex, dataOffset);
        valueCount = ByteVectorReadPackUint(state->parsed, &readIndex);
        dataSize = ByteVectorReadPackUint(state->parsed, &readIndex);
        controlSize = ByteVectorReadPackUint(state->parsed, &readIndex);

        for (stop = readIndex + dataSize, value = 0; readIndex < stop;)
        {
            if (setError(state, IntVectorAdd(&state->data, readIndex)) || /* VALUE_OFFSET */
                setError(state, IntVectorAdd(&state->data, VALUE_UNUSED))) /* VALUE_NEWINDEX */
            {
                return;
            }
            switch (ByteVectorRead(state->parsed, &readIndex))
            {
            case DATAOP_NULL:
            case DATAOP_TRUE:
            case DATAOP_FALSE:
                break;

            case DATAOP_INTEGER:
                ByteVectorSkipPackInt(state->parsed, &readIndex);
                break;

            case DATAOP_STRING:
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                break;

            case DATAOP_LIST:
                valueCount = ByteVectorReadPackUint(state->parsed, &readIndex);
                while (valueCount-- > 0)
                {
                    ByteVectorSkipPackUint(state->parsed, &readIndex);
                }
                break;

            case DATAOP_CONDITION:
                readIndex += 3 * (uint)sizeof(uint);
                break;

            case DATAOP_PARAMETER:
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                break;

            case DATAOP_RETURN:
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                break;

            case DATAOP_STACKFRAME:
                break;

            case DATAOP_STACKFRAME_ABSOLUTE:
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                break;

            case DATAOP_EQUALS:
            case DATAOP_ADD:
            case DATAOP_SUB:
            case DATAOP_INDEXED_ACCESS:
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                ByteVectorSkipPackUint(state->parsed, &readIndex);
                break;

            default:
                assert(false);
                break;
            }
        }

        for (stop = readIndex + controlSize; readIndex < stop;)
        {
            switch (ByteVectorRead(state->parsed, &readIndex))
            {
            case OP_RETURN:
                break;

            case OP_BRANCH:
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                readIndex += (uint)sizeof(uint);
                break;

            case OP_JUMP:
                readIndex += (uint)sizeof(uint);
                break;

            case OP_INVOKE_NATIVE:
                readIndex++;
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                markArguments(state, dataOffset, &readIndex);
                break;

            case OP_INVOKE_TARGET:
                readIndex++;
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                markArguments(state, dataOffset, &readIndex);
                break;

            case OP_COND_INVOKE:
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                markArguments(state, dataOffset, &readIndex);
                break;

            default:
                assert(false);
                break;
            }
        }
    }
}

static void allocateValues(State *restrict state)
{
    uint dataOffset = 0;
    uint valueCount;
    uint value;
    uint newValue;

    while (dataOffset < IntVectorSize(&state->data))
    {
        valueCount = getValueCount(state, dataOffset);

        newValue = 0;
        for (value = 0; value < valueCount; value++)
        {
            if (getNewIndex(state, dataOffset, value) == VALUE_USED_UNALLOCATED &&
                getValueInstruction(state, dataOffset, value) ==
                DATAOP_PARAMETER)
            {
                setNewIndex(state, dataOffset, value, newValue++);
            }
        }
        for (value = 0; value < valueCount; value++)
        {
            if (getNewIndex(state, dataOffset, value) == VALUE_USED_UNALLOCATED)
            {
                setNewIndex(state, dataOffset, value, newValue++);
            }
        }

        dataOffset += OFFSET_ENTRY_SIZE + valueCount * VALUE_ENTRY_SIZE;
    }
}

static void writeValue(State *restrict state,
                       bytevector *restrict bytecode,
                       bytevector *restrict valueBytecode,
                       uint dataOffset, uint value, uint newValue)
{
    uint offset = getValueOffset(state, dataOffset, value);
    uint valueCount;
    uint stackframe;
    uint returnIndex;
    byte op;

    if (setError(state, ByteVectorAddPackUint(bytecode, ByteVectorSize(valueBytecode))))
    {
        return;
    }

    op = ByteVectorRead(state->parsed, &offset);
    switch (op)
    {
    case DATAOP_NULL:
    case DATAOP_TRUE:
    case DATAOP_FALSE:
    case DATAOP_STACKFRAME:
        if (setError(state, ByteVectorAdd(valueBytecode, op)))
        {
            return;
        }
        break;

    case DATAOP_INTEGER:
        if (setError(state, ByteVectorAdd(valueBytecode, op)) ||
            setError(state,
                     ByteVectorAddPackInt(
                         valueBytecode,
                         ByteVectorGetPackInt(state->parsed, offset))))
        {
            return;
        }
        break;

    case DATAOP_STRING:
        if (setError(state, ByteVectorAdd(valueBytecode, op)) ||
            setError(state, ByteVectorAddPackUint(
                         valueBytecode,
                         ByteVectorGetPackUint(state->parsed, offset))))
        {
            return;
        }
        break;

    case DATAOP_LIST:
        if (setError(state, ByteVectorAdd(valueBytecode, op)))
        {
            return;
        }
        valueCount = ByteVectorReadPackUint(state->parsed, &offset);
        if (setError(state, ByteVectorAddPackUint(valueBytecode, valueCount)))
        {
            return;
        }
        while (valueCount-- > 0)
        {
            if (setError(
                    state,
                    ByteVectorAddPackUint(
                        valueBytecode,
                        newValue - getAllocatedNewIndex(
                            state, dataOffset,
                            ByteVectorReadPackUint(state->parsed, &offset)))))
            {
                return;
            }
        }
        break;

    case DATAOP_CONDITION:
        if (setError(state, ByteVectorAdd(valueBytecode, op)) ||
            setError(
                state,
                ByteVectorAddPackUint(
                    valueBytecode,
                    newValue - getAllocatedNewIndex(
                        state, dataOffset,
                        ByteVectorReadUint(state->parsed, &offset)))) ||
            setError(
                state,
                ByteVectorAddPackUint(
                    valueBytecode,
                    newValue - getAllocatedNewIndex(
                        state, dataOffset,
                        ByteVectorReadUint(state->parsed, &offset)))) ||
            setError(
                state,
                ByteVectorAddPackUint(
                    valueBytecode,
                    newValue - getAllocatedNewIndex(
                        state, dataOffset,
                        ByteVectorReadUint(state->parsed, &offset)))))
        {
            return;
        }
        break;

    case DATAOP_PARAMETER:
        if (setError(state, ByteVectorAdd(valueBytecode, op)) ||
            setError(state, ByteVectorAddPackUint(
                         valueBytecode,
                         ByteVectorGetPackUint(state->parsed, offset))))
        {
            return;
        }
        break;

    case DATAOP_RETURN:
        if (setError(state, ByteVectorAdd(valueBytecode, op)))
        {
            return;
        }
        stackframe = ByteVectorReadPackUint(state->parsed, &offset);
        returnIndex = ByteVectorReadPackUint(state->parsed, &offset);
        if (setError(state, ByteVectorAddPackUint(
                         valueBytecode,
                         newValue - getAllocatedNewIndex(
                             state, dataOffset, stackframe))) ||
            setError(state, ByteVectorAddPackUint(
                         valueBytecode,
                         getAllocatedNewIndex(
                             state,
                             ByteVectorGetPackUint(
                                 state->parsed,
                                 getValueOffset(state, dataOffset,
                                                stackframe) + 1),
                             returnIndex))))
        {
            return;
        }
        break;

    case DATAOP_STACKFRAME_ABSOLUTE:
        if (setError(state, ByteVectorAdd(valueBytecode, DATAOP_STACKFRAME)))
        {
            return;
        }
        ByteVectorSkipPackUint(state->parsed, &offset);
        break;

    case DATAOP_EQUALS:
    case DATAOP_ADD:
    case DATAOP_SUB:
    case DATAOP_INDEXED_ACCESS:
        if (setError(state, ByteVectorAdd(valueBytecode, op)) ||
            setError(state, ByteVectorAddPackUint(
                         valueBytecode,
                         newValue - getAllocatedNewIndex(
                             state, dataOffset,
                             ByteVectorReadPackUint(state->parsed,
                                                    &offset)))) ||
            setError(state, ByteVectorAddPackUint(
                         valueBytecode,
                         newValue - getAllocatedNewIndex(
                             state, dataOffset,
                             ByteVectorReadPackUint(state->parsed,
                                                    &offset)))))
        {
            return;
        }
        break;

    default:
        assert(false);
        break;
    }
}

static boolean writeArguments(State *restrict state,
                              bytevector *restrict bytecode,
                              uint dataOffset, uint *restrict readIndex)
{
    uint argumentCount = ByteVectorReadPackUint(state->parsed,
                                                readIndex);
    if (setError(state, ByteVectorAddPackUint(bytecode, argumentCount)))
    {
        return false;
    }
    while (argumentCount-- > 0)
    {
        if (setError(state, ByteVectorAddPackUint(
                         bytecode,
                         getAllocatedNewIndex(
                             state, dataOffset,
                             ByteVectorReadPackUint(state->parsed,
                                                    readIndex)))))
        {
            return false;
        }
    }
    return true;
}

static void writeBytecode(State *restrict state,
                          bytevector *restrict bytecode,
                          bytevector *restrict valueBytecode)
{
    uint dataOffset = 0;
    uint readIndex;
    uint dataSize;
    uint controlSize;
    uint parsedControlBase;
    uint bytecodeControlBase;
    uint stop;
    uint valueCount;
    uint usedValueCount;
    uint value;
    uint newValue;
    byte op;
    uint branchOffset;
    uint target;
    uint i;
    intvector branchOffsets;
    intvector branches;

    IntVectorInit(&branchOffsets);
    IntVectorInit(&branches);
    while (dataOffset < IntVectorSize(&state->data))
    {
        readIndex = getParsedOffset(state, dataOffset) + (uint)sizeof(uint);
        valueCount = ByteVectorReadPackUint(state->parsed, &readIndex);
        dataSize = ByteVectorReadPackUint(state->parsed, &readIndex);
        controlSize = ByteVectorReadPackUint(state->parsed, &readIndex);

        usedValueCount = 0;
        for (value = valueCount; value-- > 0;)
        {
            if (isUsed(state, dataOffset, value))
            {
                usedValueCount++;
            }
        }

        setBytecodeOffset(state, dataOffset, ByteVectorSize(bytecode));
        if (setError(state, ByteVectorAddPackUint(bytecode, usedValueCount)))
        {
            return;
        }

        for (newValue = 0; newValue != usedValueCount;)
        {
            for (value = 0; value < valueCount; value++)
            {
                if (getNewIndex(state, dataOffset, value) == newValue)
                {
                    if (isUsed(state, dataOffset, value))
                    {
                        writeValue(state, bytecode, valueBytecode, dataOffset,
                                   value, newValue);
                        if (state->error)
                        {
                            return;
                        }
                    }
                    newValue++;
                }
            }
        }

        readIndex += dataSize;
        parsedControlBase = readIndex;
        bytecodeControlBase = ByteVectorSize(bytecode);
        IntVectorSetSize(&branches, 0);
        if (setError(state, IntVectorSetSize(&branchOffsets, controlSize)))
        {
            return;
        }
        for (stop = readIndex + controlSize; readIndex < stop;)
        {
            IntVectorSet(&branchOffsets, readIndex - parsedControlBase,
                         ByteVectorSize(bytecode) - bytecodeControlBase);
            op = ByteVectorRead(state->parsed, &readIndex);
            if (setError(state, ByteVectorAdd(bytecode, op)))
            {
                return;
            }
            switch (op)
            {
            case OP_RETURN:
                break;

            case OP_BRANCH:
                if (setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 getAllocatedNewIndex(
                                     state, dataOffset,
                                     ByteVectorReadPackUint(state->parsed,
                                                            &readIndex)))))
                {
                    return;
                }
                /* fallthrough */
            case OP_JUMP:
                if (setError(state, IntVectorAdd(&branches,
                                                 ByteVectorSize(bytecode))))
                {
                    return;
                }
                target = ByteVectorReadUint(state->parsed, &readIndex);
                if (setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 readIndex - parsedControlBase + target)))
                {
                    return;
                }
                break;

            case OP_INVOKE_NATIVE:
                if (/* native function */
                    setError(state, ByteVectorAdd(
                                 bytecode,
                                 ByteVectorRead(state->parsed, &readIndex))) ||
                    /* stackframe value */
                    setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 getAllocatedNewIndex(
                                     state, dataOffset,
                                     ByteVectorReadPackUint(state->parsed,
                                                            &readIndex)))) ||
                    !writeArguments(state, bytecode, dataOffset, &readIndex))
                {
                    return;
                }
                break;

            case OP_INVOKE_TARGET:
                if (/* target */
                    setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 ByteVectorReadPackUint(state->parsed,
                                                        &readIndex))) ||
                    /* stackframe value */
                    setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 getAllocatedNewIndex(
                                     state, dataOffset,
                                     ByteVectorReadPackUint(state->parsed,
                                                            &readIndex)))) ||
                    !writeArguments(state, bytecode, dataOffset, &readIndex))
                {
                    return;
                }
                break;

            case OP_COND_INVOKE:
                /* condition */
                if (setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 ByteVectorReadPackUint(state->parsed,
                                                        &readIndex))))
                {
                    return;
                }
                value = ByteVectorReadPackUint(state->parsed, &readIndex);
                assert(ByteVectorGet(state->parsed, getValueOffset(
                                         state, dataOffset, value)) ==
                       DATAOP_STACKFRAME_ABSOLUTE);
                if (/* function */
                    setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 getBytecodeOffset(
                                     state,
                                     ByteVectorGetPackUint(
                                         state->parsed,
                                         getValueOffset(state,
                                                        dataOffset,
                                                        value) + 1)))) ||
                    /* stackframe value */
                    setError(state, ByteVectorAddPackUint(
                                 bytecode,
                                 getAllocatedNewIndex(
                                     state, dataOffset, value))) ||
                    !writeArguments(state, bytecode, dataOffset, &readIndex))
                {
                    return;
                }
                break;

            default:
                assert(false);
                break;
            }
        }
        if (setError(state, ByteVectorAdd(bytecode, (byte)-1)))
        {
            return;
        }

        for (i = IntVectorSize(&branches); i-- > 0;)
        {
            branchOffset = IntVectorGet(&branches, i);
            ByteVectorSetPackUint(
                bytecode, branchOffset,
                IntVectorGet(
                    &branchOffsets,
                    ByteVectorGetPackUint(bytecode, branchOffset)) -
                branchOffset + bytecodeControlBase -
                ByteVectorGetPackUintSize(bytecode, branchOffset));
        }

        dataOffset += OFFSET_ENTRY_SIZE + valueCount * VALUE_ENTRY_SIZE;
    }
    IntVectorDispose(&branchOffsets);
    IntVectorDispose(&branches);
}

ErrorCode BytecodeGeneratorExecute(bytevector *restrict parsed,
                                   bytevector *restrict bytecode,
                                   bytevector *restrict valueBytecode)
{
    State state;
    targetref target;

    if (DUMP_PARSED)
    {
        dumpParsed(parsed);
    }

    state.parsed = parsed;
    IntVectorInit(&state.data);
    state.error = NO_ERROR;
    markUsedValues(&state);
    if (state.error)
    {
        return state.error;
    }
    allocateValues(&state);
    writeBytecode(&state, bytecode, valueBytecode);
    if (state.error)
    {
        return state.error;
    }

    for (target = TargetIndexGetFirstTarget();
         target;
         target = TargetIndexGetNextTarget(target))
    {
        TargetIndexSetBytecodeOffset(
            target,
            getBytecodeOffset(
                &state,
                getDataOffset(&state,
                              TargetIndexGetBytecodeOffset(target))));
    }

    if (DUMP_STATE)
    {
        dumpState(&state);
    }
    if (DUMP_BYTECODE)
    {
        dumpValueBytecode(valueBytecode);
        dumpBytecode(bytecode);
    }

    IntVectorDispose(&state.data);
    return NO_ERROR;
}
