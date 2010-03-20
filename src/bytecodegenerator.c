#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"
#include "instruction.h"
#include "bytecodegenerator.h"

static const uint DUMP_PARSED = 0;
static const uint DUMP_BYTECODE = 0;
static const uint DUMP_STATE = 0;

typedef struct
{
    bytevector *parsed;
    intvector data;
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
            case DATAOP_STRING:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: string %d:\"%s\"\n", ip, value,
                       StringPoolGetString((stringref)value));
                break;
            case DATAOP_PHI_VARIABLE:
                condition = ByteVectorReadUint(parsed, &readIndex);
                value = ByteVectorReadUint(parsed, &readIndex);
                printf("%d: phi variable condition=%d %d %d\n", ip, condition,
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
            case DATAOP_STACKFRAME_ABSOLUTE:
                function = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: stackframe function=%d\n", ip, function);
                break;
            case DATAOP_STACKFRAME_NATIVE:
                printf("%d: stackframe native\n", ip);
                break;
            case DATAOP_EQUALS:
                value = ByteVectorReadPackUint(parsed, &readIndex);
                printf("%d: equals %d %d\n", ip,
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
                           ByteVectorReadUint(parsed, &readIndex));
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
    uint stackframe;

    printf("Dump value bytecode\n");
    for (readIndex = 0; readIndex < ByteVectorSize(bytecode);)
    {
        valueOffset = readIndex;
        switch (ByteVectorRead(bytecode, &readIndex))
        {
        case DATAOP_NULL:
            printf("%d: null\n", valueOffset);
            break;
        case DATAOP_TRUE:
            printf("%d: true\n", valueOffset);
            break;
        case DATAOP_FALSE:
            printf("%d: false\n", valueOffset);
            break;
        case DATAOP_STRING:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("%d: string %d: \"%s\"\n", valueOffset,
                   value, StringPoolGetString((stringref)value));
            break;
        case DATAOP_PHI_VARIABLE:
            condition = ByteVectorReadPackUint(bytecode, &readIndex);
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("%d: phi variable condition=-%d -%d -%d\n",
                   valueOffset, condition, value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;
        case DATAOP_PARAMETER:
            printf("%d: parameter name=%s\n", valueOffset,
                   StringPoolGetString((stringref)ByteVectorReadPackUint(
                                           bytecode, &readIndex)));
            break;
        case DATAOP_RETURN:
            stackframe = ByteVectorReadPackUint(bytecode, &readIndex);
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("%d: return %d from %d\n",
                   valueOffset, value, stackframe);
            break;
        case DATAOP_STACKFRAME_ABSOLUTE:
            printf("%d: stackframe function=%d\n",
                   valueOffset,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;
        case DATAOP_STACKFRAME_NATIVE:
            printf("%d: stackframe native\n", valueOffset);
            break;
        case DATAOP_EQUALS:
            value = ByteVectorReadPackUint(bytecode, &readIndex);
            printf("%d: equals -%d -%d\n", valueOffset, value,
                   ByteVectorReadPackUint(bytecode, &readIndex));
            break;
        default:
            printf("%d: %d\n", valueOffset, ByteVectorGet(bytecode, readIndex - 1));
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

    printf("Dump bytecode\n");

    for (readIndex = 0; readIndex < ByteVectorSize(bytecode);)
    {
        function = readIndex;
        valueCount = ByteVectorReadPackUint(bytecode, &readIndex);
        printf("function %d, value count=%d\n", function, valueCount);
        for (value = 0; value < valueCount; value++)
        {
            printf(" value %d at %d\n", value,
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
                printf("%d: return\n", ip);
                break;
            case OP_BRANCH:
                condition = ByteVectorReadPackUint(bytecode, &readIndex);
                target = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: branch condition=%d target=%d\n",
                       ip, condition, readIndex + target);
                break;
            case OP_JUMP:
                target = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: jump %d\n", ip, readIndex + target);
                break;
            case OP_INVOKE_NATIVE:
                function = ByteVectorRead(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: invoke native function=%d, arguments=%d, stackframe=%d\n",
                       ip, function, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(bytecode, &readIndex));
                }
                break;
            case OP_COND_INVOKE:
                condition = ByteVectorReadPackUint(bytecode, &readIndex);
                value = ByteVectorReadPackUint(bytecode, &readIndex);
                argumentCount = ByteVectorReadPackUint(bytecode, &readIndex);
                printf("%d: cond_invoke condition=%d, arguments=%d, stackframe=%d\n",
                       ip, condition, argumentCount, value);
                for (i = 0; i < argumentCount; i++)
                {
                    printf("  %d: argument %d\n", i,
                           ByteVectorReadPackUint(bytecode, &readIndex));
                }
                break;
            default:
                printf("%d: %d\n", ip, op);
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

    checkValueIndex(state, dataOffset, value);
    if (!isUsed(state, dataOffset, value))
    {
        setNewIndex(state, dataOffset, value, VALUE_USED_UNALLOCATED);
        readIndex = getValueOffset(state, dataOffset, value);
        switch (ByteVectorGet(state->parsed, readIndex++))
        {
        case DATAOP_PHI_VARIABLE:
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

static void markUsedValues(State *state)
{
    uint readIndex;
    uint stop;
    uint dataOffset;
    uint dataSize;
    uint controlSize;
    uint valueCount;
    uint argumentCount;
    uint value;

    for (readIndex = 0; readIndex < ByteVectorSize(state->parsed);)
    {
        dataOffset = IntVectorSize(&state->data);
        IntVectorAdd(&state->data, readIndex); /* PARSED_OFFSET */
        IntVectorAdd(&state->data, 0); /* BYTECODE_OFFSET */

        ByteVectorWriteUint(state->parsed, &readIndex, dataOffset);
        valueCount = ByteVectorReadPackUint(state->parsed, &readIndex);
        dataSize = ByteVectorReadPackUint(state->parsed, &readIndex);
        controlSize = ByteVectorReadPackUint(state->parsed, &readIndex);

        for (stop = readIndex + dataSize, value = 0; readIndex < stop;)
        {
            IntVectorAdd(&state->data, readIndex); /* VALUE_OFFSET */
            IntVectorAdd(&state->data, VALUE_UNUSED); /* VALUE_NEWINDEX */
            switch (ByteVectorRead(state->parsed, &readIndex))
            {
            case DATAOP_NULL:
            case DATAOP_TRUE:
            case DATAOP_FALSE:
                break;
            case DATAOP_STRING:
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                break;
            case DATAOP_PHI_VARIABLE:
                readIndex += 3 * (uint)sizeof(uint);
                break;
            case DATAOP_PARAMETER:
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                break;
            case DATAOP_RETURN:
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                break;
            case DATAOP_STACKFRAME_ABSOLUTE:
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                break;
            case DATAOP_STACKFRAME_NATIVE:
                break;
            case DATAOP_EQUALS:
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
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
                for (argumentCount = ByteVectorReadPackUint(state->parsed, &readIndex);
                     argumentCount > 0;
                     argumentCount--)
                {
                    useValue(state, dataOffset, ByteVectorReadUint(state->parsed, &readIndex));
                }
                break;
            case OP_COND_INVOKE:
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                useValue(state, dataOffset, ByteVectorReadPackUint(state->parsed, &readIndex));
                argumentCount = ByteVectorReadPackUint(state->parsed, &readIndex);
                while (argumentCount > 0)
                {
                    readIndex += ByteVectorGetPackUintSize(state->parsed, readIndex);
                    argumentCount--;
                }
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
    uint stackframe;
    uint returnIndex;
    byte op;

    ByteVectorAddPackUint(bytecode, ByteVectorSize(valueBytecode));

    op = ByteVectorRead(state->parsed, &offset);
    ByteVectorAdd(valueBytecode, op);
    switch (op)
    {
    case DATAOP_NULL:
    case DATAOP_TRUE:
    case DATAOP_FALSE:
        break;
    case DATAOP_STRING:
        ByteVectorAddPackUint(valueBytecode,
                              ByteVectorGetPackUint(state->parsed, offset));
        break;
    case DATAOP_PHI_VARIABLE:
        ByteVectorAddPackUint(
            valueBytecode,
            newValue - getAllocatedNewIndex(state, dataOffset,
                                            ByteVectorReadUint(state->parsed, &offset)));
        ByteVectorAddPackUint(
            valueBytecode,
            newValue - getAllocatedNewIndex(state, dataOffset,
                                            ByteVectorReadUint(state->parsed, &offset)));
        ByteVectorAddPackUint(
            valueBytecode,
            newValue - getAllocatedNewIndex(state, dataOffset,
                                            ByteVectorReadUint(state->parsed, &offset)));
        break;
    case DATAOP_PARAMETER:
        ByteVectorAddPackUint(valueBytecode,
                              ByteVectorGetPackUint(state->parsed, offset));
        break;
    case DATAOP_RETURN:
        stackframe = ByteVectorReadPackUint(state->parsed, &offset);
        returnIndex = ByteVectorReadPackUint(state->parsed, &offset);
        ByteVectorAddPackUint(
            valueBytecode,
            getAllocatedNewIndex(state, dataOffset, stackframe));
        ByteVectorAddPackUint(
            valueBytecode,
            getAllocatedNewIndex(
                state,
                ByteVectorGetPackUint(state->parsed,
                                      getValueOffset(state, dataOffset,
                                                     stackframe) + 1),
                returnIndex));
        break;
    case DATAOP_STACKFRAME_ABSOLUTE:
        ByteVectorAddPackUint(
            valueBytecode,
            getBytecodeOffset(
                state,
                getDataOffset(state,
                              ByteVectorReadPackUint(state->parsed, &offset))));
        break;
    case DATAOP_STACKFRAME_NATIVE:
        break;
    case DATAOP_EQUALS:
        ByteVectorAddPackUint(
            valueBytecode,
            newValue - getAllocatedNewIndex(state, dataOffset,
                                            ByteVectorReadPackUint(state->parsed, &offset)));
        ByteVectorAddPackUint(
            valueBytecode,
            newValue - getAllocatedNewIndex(state, dataOffset,
                                            ByteVectorReadPackUint(state->parsed, &offset)));
        break;
    default:
        assert(false);
        break;
    }
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
    uint argumentCount;
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
        ByteVectorAddPackUint(bytecode, usedValueCount);

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
                    }
                    newValue++;
                }
            }
        }

        readIndex += dataSize;
        parsedControlBase = readIndex;
        bytecodeControlBase = ByteVectorSize(bytecode);
        IntVectorSetSize(&branches, 0);
        IntVectorSetSize(&branchOffsets, controlSize);
        for (stop = readIndex + controlSize; readIndex < stop;)
        {
            IntVectorSet(&branchOffsets, readIndex - parsedControlBase,
                         ByteVectorSize(bytecode) - bytecodeControlBase);
            op = ByteVectorRead(state->parsed, &readIndex);
            ByteVectorAdd(bytecode, op);
            switch (op)
            {
            case OP_RETURN:
                break;
            case OP_BRANCH:
                ByteVectorAddPackUint(
                    bytecode,
                    getAllocatedNewIndex(state, dataOffset,
                                         ByteVectorReadPackUint(state->parsed,
                                                                &readIndex)));
                /* fallthrough */
            case OP_JUMP:
                IntVectorAdd(&branches, ByteVectorSize(bytecode));
                target = ByteVectorReadUint(state->parsed, &readIndex);
                ByteVectorAddPackUint(bytecode,
                                      readIndex - parsedControlBase + target);
                break;
            case OP_INVOKE_NATIVE:
                ByteVectorAdd(
                    bytecode, ByteVectorRead(state->parsed, &readIndex));
                ByteVectorAddPackUint(
                    bytecode,
                    getAllocatedNewIndex(state, dataOffset,
                                         ByteVectorReadPackUint(state->parsed,
                                                                &readIndex)));
                argumentCount = ByteVectorReadPackUint(state->parsed,
                                                       &readIndex);
                ByteVectorAddPackUint(bytecode, argumentCount);
                while (argumentCount-- > 0)
                {
                    ByteVectorAddPackUint(
                        bytecode,
                        getAllocatedNewIndex(state, dataOffset,
                                             ByteVectorReadUint(state->parsed,
                                                                &readIndex)));
                }
                break;
            case OP_COND_INVOKE:
                ByteVectorAddPackUint(
                    bytecode,
                    ByteVectorReadPackUint(state->parsed, &readIndex));
                ByteVectorAddPackUint(
                    bytecode,
                    getAllocatedNewIndex(state, dataOffset,
                                         ByteVectorReadPackUint(state->parsed,
                                                                &readIndex)));
                argumentCount = ByteVectorReadPackUint(state->parsed,
                                                       &readIndex);
                ByteVectorAddPackUint(bytecode, argumentCount);
                while (argumentCount-- > 0)
                {
                    ByteVectorAddPackUint(
                        bytecode,
                        getAllocatedNewIndex(state, dataOffset,
                                             ByteVectorReadPackUint(state->parsed,
                                                                    &readIndex)));
                }
                break;
            default:
                assert(false);
                break;
            }
        }
        ByteVectorAdd(bytecode, (byte)-1);

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

void BytecodeGeneratorExecute(bytevector *restrict parsed,
                              bytevector *restrict bytecode,
                              bytevector *restrict valueBytecode)
{
    State state;
    uint target;

    if (DUMP_PARSED)
    {
        dumpParsed(parsed);
    }

    state.parsed = parsed;
    IntVectorInit(&state.data);
    markUsedValues(&state);
    allocateValues(&state);
    writeBytecode(&state, bytecode, valueBytecode);

    for (target = TargetIndexGetTargetCount(); target-- > 0;)
    {
        TargetIndexSetBytecodeOffset(
            (targetref)target,
            getBytecodeOffset(
                &state,
                getDataOffset(&state,
                              TargetIndexGetParsedOffset((targetref)target))));
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
}
