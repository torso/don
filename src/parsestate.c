#include <stdarg.h>
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
#include "namespace.h"
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

static attrprintf(2, 3) void setError(ParseState *state,
                                      const char *format, ...)
{
    va_list args;

    va_start(args, format);
    LogParseError(state->file, state->line, format, args);
    va_end(args);
}

static void writeBackwardsJump(ParseState *state, uint target)
{
    ByteVectorAdd(state->bytecode, OP_JUMP);
    ByteVectorAddInt(
        state->bytecode,
        (int)(target - ByteVectorSize(state->bytecode) - sizeof(int)));
}

static uint getLocalsCount(ParseState *state)
{
    ParseStateCheck(state);
    return (uint)(IntHashMapSize(&state->locals) + state->unnamedVariables);
}

static uint16 getFreeLocalIndex(ParseState *state)
{
    uint count = getLocalsCount(state);
    assert(count <= UINT16_MAX);
    if (count == UINT16_MAX)
    {
        setError(state, "Too many local variables.");
    }
    return (uint16)count;
}

static uint16 getLocalIndex(ParseState *state, stringref name)
{
    uint local;
    ParseStateCheck(state);
    local = IntHashMapGet(&state->locals, uintFromRef(name));
    if (local)
    {
        return (uint16)(local - 1);
    }
    local = getFreeLocalIndex(state);
    if (local < UINT16_MAX)
    {
        IntHashMapAdd(&state->locals, uintFromRef(name), local + 1);
    }
    return (uint16)local;
}


void ParseStateInit(ParseState *state, bytevector *bytecode,
                    namespaceref ns, functionref function,
                    fileref file, uint line, uint offset)
{
    const ParameterInfo *parameterInfo;
    uint parameterCount;
    size_t size;
    uint i;

    assert(file);
    assert(line == 1 || line <= offset);
    FileMMap(file, &state->start, &size, true);
    state->current = state->start + offset;
    state->limit = state->start + size;
    state->ns = ns;
    state->function = function;
    state->file = file;
    state->line = line;
    state->indent = 0;
    state->bytecode = bytecode;
    state->unnamedVariables = 0;
    IntHashMapInit(&state->locals, 256);
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
                    setError(state, "Multiple uses of parameter name '%s'.",
                             StringPoolGetString(parameterInfo->name));
                    return;
                }
            }
        }
    }
    IntVectorInit(&state->blockStack);
}

void ParseStateDispose(ParseState *state)
{
    ParseStateCheck(state);
    FileMUnmap(state->file);
    IntVectorDispose(&state->blockStack);
    IntHashMapDispose(&state->locals);
}


static void beginBlock(ParseState *state, BlockType type)
{
    IntVectorAdd(&state->blockStack, state->indent);
    IntVectorAdd(&state->blockStack, type);
    state->indent = 0;
}

static void beginJumpBlock(ParseState *state, BlockType type)
{
    /* MAX_UINT - 1 doesn't produce any warning when uint == size_t. */
    assert(ByteVectorSize(state->bytecode) <= UINT_MAX - 1);
    IntVectorAdd(&state->blockStack, (uint)ByteVectorSize(state->bytecode));
    beginBlock(state, type);
}

static void beginLoopBlock(ParseState *state, BlockType type,
                           size_t loopOffset)
{
    /* MAX_UINT - 1 doesn't produce any warning when uint == size_t. */
    assert(loopOffset <= UINT_MAX - 1);
    IntVectorAdd(&state->blockStack, (uint)loopOffset);
    beginJumpBlock(state, type);
}

static void writeElse(ParseState *state, BlockType type)
{
    ByteVectorAdd(state->bytecode, OP_JUMP);
    beginJumpBlock(state, type);
    ByteVectorAddInt(state->bytecode, 0);
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

        FunctionIndexSetLocals(state->function, &state->locals,
                               getLocalsCount(state));
        ParseStateWriteReturnVoid(state);
        return true;
    }

    type = (BlockType)(int)IntVectorPop(&state->blockStack);
    prevIndent = IntVectorPop(&state->blockStack);
    if (indent > prevIndent)
    {
        setError(state, "Mismatched indentation level.");
        return false;
    }

    state->indent = prevIndent;

    if (trailingElse && prevIndent <= indent)
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
            writeElse(state, BLOCK_ELSE);
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
            writeElse(state, BLOCK_CONDITION2);
            break;

        case BLOCK_CONDITION2:
            jumpOffset = IntVectorPop(&state->blockStack);
            break;

        case BLOCK_WHILE:
            jumpOffset = IntVectorPop(&state->blockStack);
            loopOffset = IntVectorPop(&state->blockStack);
            writeBackwardsJump(state, loopOffset);
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

void ParseStateBeginForwardJump(ParseState *state, Instruction instruction,
                                size_t *branch)
{
    ParseStateWriteInstruction(state, instruction);
    *branch = ByteVectorSize(state->bytecode);
    ByteVectorAddUint(state->bytecode, 0);
}

void ParseStateFinishJump(ParseState *state, size_t branch)
{
    ParseStateCheck(state);
    ByteVectorSetUint(
        state->bytecode, branch,
        (uint)(ParseStateGetJumpTarget(state) - branch - sizeof(uint)));
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


boolean ParseStateIsParameter(ParseState *state, stringref name)
{
    uint local = IntHashMapGet(&state->locals, uintFromRef(name));
    if (!local)
    {
        return false;
    }
    return local <= FunctionIndexGetParameterCount(state->function);
}

boolean ParseStateGetVariable(ParseState *state, stringref name)
{
    uint16 local = getLocalIndex(state, name);
    if (local == UINT16_MAX)
    {
        return false;
    }
    ParseStateGetUnnamedVariable(state, local);
    return true;
}

boolean ParseStateSetVariable(ParseState *state, stringref name)
{
    uint16 local = getLocalIndex(state, name);
    if (local == UINT16_MAX)
    {
        return false;
    }
    ParseStateSetUnnamedVariable(state, local);
    return true;
}

boolean ParseStateCreateUnnamedVariable(ParseState *state, uint16 *result)
{
    uint16 local = getFreeLocalIndex(state);
    if (local == UINT16_MAX)
    {
        return false;
    }
    state->unnamedVariables++;
    *result = local;
    return true;
}

void ParseStateGetUnnamedVariable(ParseState *state, uint16 variable)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_LOAD);
    ByteVectorAddUint16(state->bytecode, variable);
}

void ParseStateSetUnnamedVariable(ParseState *state, uint16 variable)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_STORE);
    ByteVectorAddUint16(state->bytecode, variable);
}


void ParseStateGetField(ParseState *state, fieldref field)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_LOAD_FIELD);
    ByteVectorAddUint(state->bytecode, FieldIndexGetIndex(field));
}

void ParseStateSetField(ParseState *state, fieldref field)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_STORE_FIELD);
    ByteVectorAddUint(state->bytecode, FieldIndexGetIndex(field));
}


void ParseStateWriteInstruction(ParseState *state, Instruction instruction)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, instruction);
}

void ParseStateWriteNullLiteral(ParseState *state)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_NULL);
}

void ParseStateWriteTrueLiteral(ParseState *state)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_TRUE);
}

void ParseStateWriteFalseLiteral(ParseState *state)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_FALSE);
}

void ParseStateWriteIntegerLiteral(ParseState *state, int value)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_INTEGER);
    ByteVectorAddInt(state->bytecode, value);
}

void ParseStateWriteStringLiteral(ParseState *state, stringref value)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_STRING);
    ByteVectorAddRef(state->bytecode, value);
}

void ParseStateWriteList(ParseState *state, uint size)
{
    ParseStateCheck(state);
    if (!size)
    {
        ParseStateWriteInstruction(state, OP_EMPTY_LIST);
        return;
    }
    ParseStateWriteInstruction(state, OP_LIST);
    ByteVectorAddUint(state->bytecode, size);
}

void ParseStateWriteFile(ParseState *state, stringref filename)
{
    ParseStateCheck(state);
    ParseStateWriteInstruction(state, OP_FILE);
    ByteVectorAddRef(state->bytecode, filename);
}

void ParseStateWriteFileset(ParseState *state, stringref pattern)
{
    ParseStateCheck(state);
    ParseStateWriteInstruction(state, OP_FILESET);
    ByteVectorAddRef(state->bytecode, pattern);
}

void ParseStateWriteBeginCondition(ParseState *state)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE);
    beginJumpBlock(state, BLOCK_CONDITION1);
    ByteVectorAddInt(state->bytecode, 0);
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


void ParseStateWriteIf(ParseState *state)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE);
    beginJumpBlock(state, BLOCK_IF);
    ByteVectorAddInt(state->bytecode, 0);
}

void ParseStateWriteWhile(ParseState *state, size_t loopTarget)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_BRANCH_FALSE);
    beginLoopBlock(state, BLOCK_WHILE, loopTarget);
    ByteVectorAddInt(state->bytecode, 0);
}

void ParseStateWriteReturn(ParseState *state, uint values)
{
    assert(values);
    assert(values <= UINT8_MAX); /* TODO: report error */
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_RETURN);
    ByteVectorAdd(state->bytecode, (uint8)values);
}

void ParseStateWriteReturnVoid(ParseState *state)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_RETURN_VOID);
}

void ParseStateWriteInvocation(ParseState *state,
                               functionref function, uint argumentCount,
                               uint returnValues)
{
    assert(argumentCount <= UINT16_MAX); /* TODO: report error */
    assert(returnValues <= UINT8_MAX); /* TODO: report error */
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_INVOKE);
    ByteVectorAddRef(state->bytecode, function);
    ByteVectorAddUint16(state->bytecode, (uint16)argumentCount);
    ByteVectorAdd(state->bytecode, (uint8)returnValues);
}

void ParseStateWriteNativeInvocation(ParseState *state,
                                     nativefunctionref function)
{
    ParseStateCheck(state);
    ByteVectorAdd(state->bytecode, OP_INVOKE_NATIVE);
    ByteVectorAdd(state->bytecode, (byte)uintFromRef(function));
}

void ParseStateReorderStack(ParseState *state,
                            intvector *order, uint offset, uint count)
{
    uint position;
    uint baseOffset = offset;

    assert(count);
    assert(count <= UINT16_MAX);
    ParseStateCheck(state);

    ByteVectorAdd(state->bytecode, OP_REORDER_STACK);
    ByteVectorAddUint16(state->bytecode, (uint16)count);
    while (count)
    {
        position = IntVectorGet(order, offset++);
        if (position)
        {
            assert(position - 1 <= UINT16_MAX);
            ByteVectorAddUint16(state->bytecode,
                                (uint16)(position - baseOffset - 1));
            count--;
        }
    }
}
