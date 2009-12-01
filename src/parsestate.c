#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "native.h"
#include "parsestate.h"
#include "instruction.h"

static void dump(const ParseState* state)
{
    uint i, j, ip;
    uint size;
    uint data;
    printf("data, size=%d\n", ByteVectorSize(&state->data));
    for (i = 0; i < ByteVectorSize(&state->data);)
    {
        ip = i;
        switch (ByteVectorGet(&state->data, i++))
        {
        case DATAOP_STRING:
            data = ByteVectorGetPackUint(&state->data, i);
            printf("%d: string %d:\"%s\"\n", ip, data, StringPoolGetString(data));
            size = ByteVectorGetPackUintSize(&state->data, i);
            break;
        default:
            assert(false);
            break;
        }
        i += size;
        assert(i <= ByteVectorSize(&state->data));
    }
    printf("control, size=%d\n", ByteVectorSize(&state->control));
    for (i = 0; i < ByteVectorSize(&state->control);)
    {
        ip = i;
        size = 0;
        switch (ByteVectorGet(&state->control, i++))
        {
        case OP_SKIP:
            size = ByteVectorGet(&state->control, i) + 1;
            printf("%d: skip %d\n", ip, size - 1);
            for (j = 1; j < size; j++)
            {
                printf("  %d: %d\n", i + j,
                       ByteVectorGet(&state->control, i + j));
            }
            break;
        case OP_RETURN:
            printf("%d: return\n", ip);
            break;
        case OP_INVOKE_NATIVE:
            printf("%d: invoke native %d, %d\n", ip,
                   ByteVectorGet(&state->control, ip),
                   ByteVectorGetPackUint(&state->control, i + 1));
            size = 1 + ByteVectorGetPackUintSize(&state->control, i + 1);
            break;
        case OP_BRANCH:
            size = ByteVectorGetPackUintSize(&state->control, i);
            printf("%d: branch %d %d\n", ip,
                   ByteVectorGetPackUint(&state->control, i),
                   ByteVectorGetUint(&state->control, i + size));
            size += 4;
            break;
        case OP_LOOP:
            printf("%d: loop %d\n", ip, ByteVectorGetPackUint(&state->control, i));
            size = ByteVectorGetPackUintSize(&state->control, i);
            break;
        default:
            assert(false);
            break;
        }
        i += size;
        assert(i <= ByteVectorSize(&state->control));
    }
}

void ParseStateCheck(const ParseState* state)
{
    assert(state->start != null);
    assert(state->current >= state->start);
    assert(state->current <= state->start + FileIndexGetSize(state->file));
}

void ParseStateInit(ParseState* state, fileref file, uint line, uint offset)
{
    assert(file);
    assert(line == 1 || line <= offset);
    state->start = FileIndexGetContents(file);
    state->current = state->start + offset;
    state->file = file;
    state->line = line;
    IntVectorInit(&state->blocks);
    IntVectorInit(&state->locals);
    ByteVectorInit(&state->data);
    ByteVectorInit(&state->control);
}

void ParseStateDispose(ParseState* state)
{
    dump(state);
    IntVectorFree(&state->blocks);
    IntVectorFree(&state->locals);
    ByteVectorFree(&state->data);
    ByteVectorFree(&state->control);
}


boolean ParseStateBlockBegin(ParseState* state, int indent, boolean loop)
{
    ParseStateCheck(state);
    IntVectorAdd(&state->blocks, indent);
    IntVectorAdd(&state->blocks, loop);
    IntVectorAdd(&state->blocks, ByteVectorSize(&state->control));
    return true;
}

static void ParseStateBlockSetConditionOffset(ParseState* state)
{
    assert(!ParseStateBlockEmpty(state));
    IntVectorAdd(&state->blocks, ByteVectorSize(&state->control));
}

void ParseStateBlockEnd(ParseState* state)
{
    int conditionInstruction;
    int loopBegin;
    assert(!ParseStateBlockEmpty(state));
    conditionInstruction = IntVectorPop(&state->blocks);
    loopBegin = IntVectorPop(&state->blocks);
    if (IntVectorPop(&state->blocks))
    {
        ByteVectorAdd(&state->control, OP_LOOP);
        ByteVectorAddPackUint(&state->control, loopBegin);
    }
    ByteVectorSetInt(&state->control, conditionInstruction +
                     ByteVectorGetPackUintSize(&state->control,
                                               conditionInstruction + 1) + 1,
                     ByteVectorSize(&state->control));
    IntVectorPop(&state->blocks);
}

boolean ParseStateBlockEmpty(ParseState* state)
{
    ParseStateCheck(state);
    return IntVectorSize(&state->blocks) == 0;
}

int ParseStateBlockIndent(ParseState* state)
{
    assert(!ParseStateBlockEmpty(state));
    return IntVectorGet(&state->blocks, IntVectorSize(&state->blocks) - 4);
}


uint ParseStateWriteArguments(ParseState* state, uint size)
{
    uint offset;
    ParseStateCheck(state);
    assert(size <= 256 / sizeof(int));
    if (!ByteVectorAdd(&state->control, OP_SKIP) ||
        !ByteVectorAdd(&state->control, size * sizeof(int)))
    {
        return 0;
    }
    offset = ByteVectorSize(&state->control);
    ByteVectorSetSize(&state->control, offset + size * sizeof(int));
    ByteVectorFill(&state->control, offset, size * sizeof(int), 0);
    return offset;
}

void ParseStateSetArgument(ParseState* state, uint offset, int value)
{
    ParseStateCheck(state);
    ByteVectorSetInt(&state->control, offset, value);
}


int ParseStateWriteStringLiteral(ParseState* state, stringref value)
{
    int size = ByteVectorSize(&state->data);
    ParseStateCheck(state);
    return ByteVectorAdd(&state->data, DATAOP_STRING) &&
        ByteVectorAddPackUint(&state->data, value) ? size : -1;
}


boolean ParseStateWriteWhile(ParseState* state, int value)
{
    ParseStateCheck(state);
    ParseStateBlockSetConditionOffset(state);
    return ByteVectorAdd(&state->control, OP_BRANCH) &&
        ByteVectorAddPackUint(&state->control, value) &&
        ByteVectorAddInt(&state->control, 0) ? true : false;
}

boolean ParseStateWriteReturn(ParseState* state)
{
    ParseStateCheck(state);
    return ByteVectorAdd(&state->control, OP_RETURN);
}

boolean ParseStateWriteNativeInvocation(ParseState* state,
                                        nativefunctionref nativeFunction,
                                        uint argumentOffset)
{
    ParseStateCheck(state);
    assert(argumentOffset < ByteVectorSize(&state->control));
    return ByteVectorAdd(&state->control, OP_INVOKE_NATIVE) &&
        ByteVectorAdd(&state->control, nativeFunction) &&
        ByteVectorAddPackUint(&state->control, argumentOffset) ? true : false;
}
