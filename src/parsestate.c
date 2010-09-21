#include "builder.h"
#include "bytevector.h"
#include "fileindex.h"
#include "instruction.h"
#include "inthashmap.h"
#include "intvector.h"
#include "log.h"
#include "parsestate.h"
#include "targetindex.h"

typedef enum
{
    BLOCK_IF,
    BLOCK_ELSE,
    BLOCK_CONDITION1,
    BLOCK_CONDITION2,
    BLOCK_WHILE
} BlockType;


void ParseStateCheck(const ParseState *state)
{
    assert(state->start);
    assert(state->current >= state->start);
    assert(state->current <= state->start + FileIndexGetSize(state->file));
}

static void setError(ParseState *state, const char *message)
{
    ParseStateSetError(state, BUILD_ERROR);
    LogParseError(state->file, state->line, message);
}

static boolean writeBackwardsJump(ParseState *state, uint target)
{
    uint offset = ByteVectorSize(state->bytecode) + 1;
    if (ParseStateSetError(state,
                           ByteVectorAdd(state->bytecode, OP_JUMP)) ||
        ParseStateSetError(state,
                           ByteVectorAddUnpackedInt(state->bytecode, 0)))
    {
        return false;
    }
    ByteVectorSetPackInt(state->bytecode, offset,
                         (int)(target - ByteVectorSize(state->bytecode)));
    return true;
}


void ParseStateInit(ParseState *state, bytevector *bytecode,
                    targetref target, fileref file, uint line, uint offset)
{
    assert(file);
    assert(line == 1 || line <= offset);
    state->start = FileIndexGetContents(file);
    state->current = state->start + offset;
    state->target = target;
    state->file = file;
    state->line = line;
    state->indent = 0;
    state->bytecode = bytecode;
    state->error = IntVectorInit(&state->blockStack);
    if (state->error)
    {
        return;
    }
    state->error = IntHashMapInit(&state->locals, 256);
    if (state->error)
    {
        IntVectorDispose(&state->blockStack);
    }
}

void ParseStateDispose(ParseState *state)
{
    ParseStateCheck(state);
    IntVectorDispose(&state->blockStack);
    IntHashMapDispose(&state->locals);
}

boolean ParseStateSetError(ParseState *state, ErrorCode error)
{
    ParseStateCheck(state);
    state->error = error;
    return state->error ? true : false;
}


static boolean beginBlock(ParseState *state, BlockType type, uint loopOffset)
{
    IntVectorAdd(&state->blockStack, ByteVectorSize(state->bytecode));
    IntVectorAdd(&state->blockStack, state->indent);
    IntVectorAdd(&state->blockStack, type);
    IntVectorAdd(&state->blockStack, loopOffset);
    state->indent = 0;
    return true;
}

static boolean writeElse(ParseState *state, BlockType type)
{
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_JUMP)) &&
        beginBlock(state, type, 0) &&
        !ParseStateSetError(state,
                            ByteVectorAddUnpackedInt(state->bytecode, 0));
}

boolean ParseStateFinishBlock(ParseState *restrict state,
                              uint indent, boolean trailingElse)
{
    uint prevIndent;
    uint jumpOffset;
    BlockType type;
    uint loopOffset;

    ParseStateCheck(state);

    if (!IntVectorSize(&state->blockStack))
    {
        state->indent = 0;

        if (indent)
        {
            setError(state, "Mismatched indentation level.");
            return false;
        }

        state->error = TargetIndexSetLocals(state->target, &state->locals);
        return !state->error && ParseStateWriteReturnVoid(state);
    }

    loopOffset = IntVectorPop(&state->blockStack);
    type = IntVectorPop(&state->blockStack);
    prevIndent = IntVectorPop(&state->blockStack);
    jumpOffset = IntVectorPop(&state->blockStack);
    if (indent > prevIndent)
    {
        setError(state, "Mismatched indentation level.");
        return false;
    }

    state->indent = prevIndent;

    if (trailingElse)
    {
        if (type != BLOCK_IF)
        {
            setError(state, "Else without matching if.");
            return false;
        }

        if (indent == prevIndent)
        {
            state->indent = indent;
            if (!writeElse(state, BLOCK_ELSE))
            {
                return false;
            }
            state->indent = 0;
        }
    }
    else
    {
        switch (type)
        {
        case BLOCK_IF:
        case BLOCK_ELSE:
            break;

        case BLOCK_CONDITION1:
            if (!writeElse(state, BLOCK_CONDITION2))
            {
                return false;
            }
            break;

        case BLOCK_CONDITION2:
            break;

        case BLOCK_WHILE:
            if (!writeBackwardsJump(state, loopOffset))
            {
                return false;
            }
            break;
        }
    }

    ByteVectorSetPackInt(
        state->bytecode, jumpOffset,
        (int)(ByteVectorSize(state->bytecode) - jumpOffset -
              ByteVectorGetPackIntSize(state->bytecode, jumpOffset)));
    return true;
}

uint ParseStateGetJumpTarget(ParseState *state)
{
    ParseStateCheck(state);
    return ByteVectorSize(state->bytecode);
}


void ParseStateSetIndent(ParseState *state, uint indent)
{
    ParseStateCheck(state);
    assert(!state->indent);
    state->indent = indent;
}

uint ParseStateBlockIndent(ParseState *state)
{
    ParseStateCheck(state);
    return state->indent;
}


static uint getLocalIndex(ParseState *state, stringref name)
{
    uint local;
    ParseStateCheck(state);
    local = IntHashMapGet(&state->locals, (uint)name);
    if (!local)
    {
        local = IntHashMapSize(&state->locals) + 1;
        state->error = IntHashMapAdd(&state->locals, (uint)name, local);
    }
    return local - 1;
}

boolean ParseStateGetVariable(ParseState *state, stringref name)
{
    uint local = getLocalIndex(state, name);
    return !state->error &&
        !ParseStateSetError(state, ByteVectorAdd(state->bytecode, OP_LOAD)) &&
        !ParseStateSetError(state,
                            ByteVectorAddPackUint(state->bytecode, local));
}

boolean ParseStateSetVariable(ParseState *state, stringref name)
{
    uint local = getLocalIndex(state, name);
    return !state->error &&
        !ParseStateSetError(state, ByteVectorAdd(state->bytecode, OP_STORE)) &&
        !ParseStateSetError(state, ByteVectorAddPackUint(state->bytecode, local));
}


boolean ParseStateWriteNullLiteral(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state, ByteVectorAdd(state->bytecode, OP_NULL));
}

boolean ParseStateWriteTrueLiteral(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state, ByteVectorAdd(state->bytecode, OP_TRUE));
}

boolean ParseStateWriteFalseLiteral(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state, ByteVectorAdd(state->bytecode, OP_FALSE));
}

boolean ParseStateWriteIntegerLiteral(ParseState *state, int value)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_INTEGER)) &&
        !ParseStateSetError(
            state, ByteVectorAddPackInt(state->bytecode, value));
}

boolean ParseStateWriteStringLiteral(ParseState *state, stringref value)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_STRING)) &&
        !ParseStateSetError(
            state, ByteVectorAddPackUint(state->bytecode, (uint)value));
}

boolean ParseStateWriteBinaryOperation(ParseState *state, Instruction operation)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, operation));
}

boolean ParseStateWriteBeginCondition(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE)) &&
        beginBlock(state, BLOCK_CONDITION1, 0) &&
        !ParseStateSetError(state,
                            ByteVectorAddUnpackedInt(state->bytecode, 0));
}

boolean ParseStateWriteSecondConsequent(ParseState *state)
{
    ParseStateCheck(state);
    return ParseStateFinishBlock(state, state->indent, false);
}

boolean ParseStateWriteFinishCondition(ParseState *state)
{
    ParseStateCheck(state);
    return ParseStateFinishBlock(state, state->indent, false);
}


boolean ParseStateWriteIf(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE)) &&
        beginBlock(state, BLOCK_IF, 0) &&
        !ParseStateSetError(state,
                            ByteVectorAddUnpackedInt(state->bytecode, 0));
}

boolean ParseStateWriteWhile(ParseState *state, uint loopTarget)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE)) &&
        beginBlock(state, BLOCK_WHILE, loopTarget) &&
        !ParseStateSetError(state,
                            ByteVectorAddUnpackedInt(state->bytecode, 0));
}

boolean ParseStateWriteReturn(ParseState *state, uint values)
{
    assert(values);
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_RETURN)) &&
        !ParseStateSetError(state,
                            ByteVectorAddPackUint(state->bytecode, values));
}

boolean ParseStateWriteReturnVoid(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_RETURN_VOID));
}

boolean ParseStateWriteInvocation(ParseState *state,
                                  nativefunctionref nativeFunction,
                                  targetref target, uint argumentCount,
                                  uint returnValues)
{
    ParseStateCheck(state);
    if (nativeFunction >= 0)
    {
        assert(!target);
        if (ParseStateSetError(
                state, ByteVectorAdd(state->bytecode, OP_INVOKE_NATIVE)) ||
            ParseStateSetError(
                state, ByteVectorAdd(state->bytecode, (byte)nativeFunction)))
        {
            return false;
        }
    }
    else
    {
        if (ParseStateSetError(
                state, ByteVectorAdd(state->bytecode, OP_INVOKE)) ||
            ParseStateSetError(
                state, ByteVectorAddPackUint(state->bytecode, target)))
        {
            return false;
        }
    }
    return !ParseStateSetError(state, ByteVectorAddPackUint(state->bytecode,
                                                            argumentCount)) &&
        !ParseStateSetError(state, ByteVectorAddPackUint(state->bytecode,
                                                         returnValues));
}
