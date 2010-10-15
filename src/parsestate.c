#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "instruction.h"
#include "inthashmap.h"
#include "intvector.h"
#include "log.h"
#include "parsestate.h"
#include "stringpool.h"

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
    assert(state->current <= state->limit);
}

static void setError(ParseState *state, const char *message)
{
    ParseStateSetError(state, ERROR_FAIL);
    LogParseError(state->file, state->line, message);
}

static boolean writeBackwardsJump(ParseState *state, uint target)
{
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_JUMP)) &&
        !ParseStateSetError(
            state,
            ByteVectorAddInt(
                state->bytecode,
                (int)(target - ByteVectorSize(state->bytecode) - sizeof(int))));
}

static uint getLocalsCount(ParseState *state)
{
    ParseStateCheck(state);
    return (uint)(IntHashMapSize(&state->locals) + state->unnamedVariables);
}

static uint16 getFreeLocalIndex(ParseState *state)
{
    uint count = getLocalsCount(state);
    if (count == UINT16_MAX)
    {
        setError(state, "Too many local variables.");
    }
    return (uint16)getLocalsCount(state);
}

static uint16 getLocalIndex(ParseState *state, stringref name)
{
    uint local;
    ParseStateCheck(state);
    local = IntHashMapGet(&state->locals, uintFromRef(name));
    if (!local)
    {
        local = (uint)getFreeLocalIndex(state) + 1;
        if (state->error)
        {
            return 0;
        }
        state->error = IntHashMapAdd(&state->locals, uintFromRef(name), local);
    }
    return (uint16)(local - 1);
}


void ParseStateInit(ParseState *state, bytevector *bytecode,
                    functionref function, fileref file, uint line, uint offset)
{
    char errorBuffer[256];
    const ParameterInfo *parameterInfo;
    uint parameterCount;
    size_t size;
    uint i;

    assert(file);
    assert(line == 1 || line <= offset);
    state->error = FileMMap(file, &state->start, &size);
    if (state->error)
    {
        return;
    }
    state->current = state->start + offset;
    state->limit = state->start + size;
    state->function = function;
    state->file = file;
    state->line = line;
    state->indent = 0;
    state->bytecode = bytecode;
    state->unnamedVariables = 0;
    state->error = IntHashMapInit(&state->locals, 256);
    if (state->error)
    {
        return;
    }
    if (function)
    {
        parameterCount = FunctionIndexGetParameterCount(function);
        if (parameterCount)
        {
            parameterInfo = FunctionIndexGetParameterInfo(function);
            for (i = 0; i < parameterCount; i++, parameterInfo++)
            {
                if (getLocalIndex(state, parameterInfo->name) != i)
                {
                    IntHashMapDispose(&state->locals);
                    sprintf(errorBuffer,
                            "Multiple uses of parameter name '%s'.",
                            StringPoolGetString(parameterInfo->name));
                    setError(state, errorBuffer);
                    return;
                }
            }
        }
    }
    state->error = IntVectorInit(&state->blockStack);
    if (state->error)
    {
        IntHashMapDispose(&state->locals);
        return;
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


static boolean beginBlock(ParseState *state, BlockType type)
{
    IntVectorAdd(&state->blockStack, state->indent);
    IntVectorAdd(&state->blockStack, type);
    state->indent = 0;
    return true;
}

static boolean beginJumpBlock(ParseState *state, BlockType type)
{
    /* MAX_UINT - 1 doesn't produce any warning when uint == size_t. */
    assert(ByteVectorSize(state->bytecode) <= UINT_MAX - 1);
    IntVectorAdd(&state->blockStack, (uint)ByteVectorSize(state->bytecode));
    return beginBlock(state, type);
}

static boolean beginLoopBlock(ParseState *state, BlockType type,
                              size_t loopOffset)
{
    /* MAX_UINT - 1 doesn't produce any warning when uint == size_t. */
    assert(loopOffset <= UINT_MAX - 1);
    IntVectorAdd(&state->blockStack, (uint)loopOffset);
    return beginJumpBlock(state, type);
}

static boolean writeElse(ParseState *state, BlockType type)
{
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_JUMP)) &&
        beginJumpBlock(state, type) &&
        !ParseStateSetError(state, ByteVectorAddInt(state->bytecode, 0));
}

boolean ParseStateFinishBlock(ParseState *restrict state,
                              uint indent, boolean trailingElse)
{
    uint prevIndent;
    uint jumpOffset = 0;
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

        state->error = FunctionIndexSetLocals(state->function, &state->locals,
                                              getLocalsCount(state));
        return !state->error && ParseStateWriteReturnVoid(state);
    }

    type = IntVectorPop(&state->blockStack);
    prevIndent = IntVectorPop(&state->blockStack);
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

        jumpOffset = IntVectorPop(&state->blockStack);
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
            jumpOffset = IntVectorPop(&state->blockStack);
            break;

        case BLOCK_CONDITION1:
            jumpOffset = IntVectorPop(&state->blockStack);
            if (!writeElse(state, BLOCK_CONDITION2))
            {
                return false;
            }
            break;

        case BLOCK_CONDITION2:
            jumpOffset = IntVectorPop(&state->blockStack);
            break;

        case BLOCK_WHILE:
            jumpOffset = IntVectorPop(&state->blockStack);
            loopOffset = IntVectorPop(&state->blockStack);
            if (!writeBackwardsJump(state, loopOffset))
            {
                return false;
            }
            break;
        }
    }

    if (jumpOffset)
    {
        ByteVectorSetInt(
            state->bytecode, jumpOffset,
            (int)(ByteVectorSize(state->bytecode) - jumpOffset - sizeof(int)));
    }
    return true;
}

size_t ParseStateGetJumpTarget(ParseState *state)
{
    ParseStateCheck(state);
    return ByteVectorSize(state->bytecode);
}

boolean ParseStateBeginForwardJump(ParseState *state, Instruction instruction,
                                   size_t *branch)
{
    if (!ParseStateWriteInstruction(state, instruction))
    {
        return false;
    }
    *branch = ByteVectorSize(state->bytecode);
    return !ParseStateSetError(state, ByteVectorAddUint(state->bytecode, 0));
}

boolean ParseStateFinishJump(ParseState *state, size_t branch)
{
    ParseStateCheck(state);
    ByteVectorSetUint(
        state->bytecode, branch,
        (uint)(ParseStateGetJumpTarget(state) - branch - sizeof(uint)));
    return true;
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


boolean ParseStateGetVariable(ParseState *state, stringref name)
{
    uint16 local = getLocalIndex(state, name);
    return !state->error && ParseStateGetUnnamedVariable(state, local);
}

boolean ParseStateSetVariable(ParseState *state, stringref name)
{
    uint16 local = getLocalIndex(state, name);
    return !state->error && ParseStateSetUnnamedVariable(state, local);
}

uint16 ParseStateCreateUnnamedVariable(ParseState *state)
{
    uint16 local = getFreeLocalIndex(state);
    state->unnamedVariables++;
    return local;
}

boolean ParseStateGetUnnamedVariable(ParseState *state, uint16 variable)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_LOAD)) &&
        !ParseStateSetError(state,
                            ByteVectorAddUint16(state->bytecode, variable));
}

boolean ParseStateSetUnnamedVariable(ParseState *state, uint16 variable)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_STORE)) &&
        !ParseStateSetError(state,
                            ByteVectorAddUint16(state->bytecode, variable));
}


boolean ParseStateGetField(ParseState *state, fieldref field)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_LOAD_FIELD)) &&
        !ParseStateSetError(
            state, ByteVectorAddUint(state->bytecode,
                                     FieldIndexGetIndex(field)));
}

boolean ParseStateSetField(ParseState *state, fieldref field)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_STORE_FIELD)) &&
        !ParseStateSetError(
            state, ByteVectorAddUint(state->bytecode,
                                     FieldIndexGetIndex(field)));
}


boolean ParseStateWriteInstruction(ParseState *state, Instruction instruction)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, instruction));
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
            state, ByteVectorAddInt(state->bytecode, value));
}

boolean ParseStateWriteStringLiteral(ParseState *state, stringref value)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_STRING)) &&
        !ParseStateSetError(
            state, ByteVectorAddRef(state->bytecode, value));
}

boolean ParseStateWriteList(ParseState *state, uint size)
{
    ParseStateCheck(state);
    return ParseStateWriteInstruction(state, OP_LIST) &&
        !ParseStateSetError(state, ByteVectorAddUint(state->bytecode, size));
}

boolean ParseStateWriteFile(ParseState *state, stringref filename)
{
    ParseStateCheck(state);
    return ParseStateWriteInstruction(state, OP_FILE) &&
        !ParseStateSetError(state,
                            ByteVectorAddRef(state->bytecode, filename));
}

boolean ParseStateWriteFileset(ParseState *state, stringref pattern)
{
    ParseStateCheck(state);
    return ParseStateWriteInstruction(state, OP_FILESET) &&
        !ParseStateSetError(state,
                            ByteVectorAddRef(state->bytecode, pattern));
}

boolean ParseStateWriteBeginCondition(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE)) &&
        beginJumpBlock(state, BLOCK_CONDITION1) &&
        !ParseStateSetError(state, ByteVectorAddInt(state->bytecode, 0));
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
        beginJumpBlock(state, BLOCK_IF) &&
        !ParseStateSetError(state, ByteVectorAddInt(state->bytecode, 0));
}

boolean ParseStateWriteWhile(ParseState *state, size_t loopTarget)
{
    ParseStateCheck(state);
    return !ParseStateSetError(
        state, ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE)) &&
        beginLoopBlock(state, BLOCK_WHILE, loopTarget) &&
        !ParseStateSetError(state, ByteVectorAddInt(state->bytecode, 0));
}

boolean ParseStateWriteReturn(ParseState *state, uint values)
{
    assert(values);
    assert(values <= UINT8_MAX); /* TODO: report error */
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_RETURN)) &&
        !ParseStateSetError(state,
                            ByteVectorAdd(state->bytecode, (uint8)values));
}

boolean ParseStateWriteReturnVoid(ParseState *state)
{
    ParseStateCheck(state);
    return !ParseStateSetError(state,
                               ByteVectorAdd(state->bytecode, OP_RETURN_VOID));
}

boolean ParseStateWriteInvocation(ParseState *state,
                                  nativefunctionref nativeFunction,
                                  functionref function, uint argumentCount,
                                  uint returnValues)
{
    assert(argumentCount <= UINT16_MAX); /* TODO: report error */
    assert(returnValues <= UINT8_MAX); /* TODO: report error */
    ParseStateCheck(state);
    if (nativeFunction)
    {
        assert(!function);
        if (ParseStateSetError(
                state, ByteVectorAdd(state->bytecode, OP_INVOKE_NATIVE)) ||
            ParseStateSetError(
                state, ByteVectorAdd(state->bytecode,
                                     (byte)uintFromRef(nativeFunction))))
        {
            return false;
        }
    }
    else
    {
        if (ParseStateSetError(
                state, ByteVectorAdd(state->bytecode, OP_INVOKE)) ||
            ParseStateSetError(
                state, ByteVectorAddRef(state->bytecode, function)))
        {
            return false;
        }
    }
    return !ParseStateSetError(state,
                               ByteVectorAddUint16(state->bytecode,
                                                   (uint16)argumentCount)) &&
        !ParseStateSetError(state, ByteVectorAdd(state->bytecode,
                                                 (uint8)returnValues));
}

boolean ParseStateReorderStack(ParseState *state,
                               intvector *order, uint offset, uint count)
{
    uint position;
    uint baseOffset = offset;

    assert(count);
    assert(count <= UINT16_MAX);
    ParseStateCheck(state);

    if (ParseStateSetError(
            state, ByteVectorAdd(state->bytecode, OP_REORDER_STACK)) ||
        ParseStateSetError(
            state, ByteVectorAddUint16(state->bytecode, (uint16)count)))
    {
        return false;
    }
    while (count)
    {
        position = IntVectorGet(order, offset++);
        if (position)
        {
            assert(position - 1 <= UINT16_MAX);
            if (ParseStateSetError(
                    state, ByteVectorAddUint16(state->bytecode,
                                               (uint16)(position - baseOffset - 1))))
            {
                return false;
            }
            count--;
        }
    }
    return true;
}
