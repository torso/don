#include <stdarg.h>
#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "heap.h"
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
    LogParseError(state->filename, state->line, format, args);
    va_end(args);
}

static void writeBackwardsJump(ParseState *state, uint target)
{
    BVAdd(state->bytecode, OP_JUMP);
    BVAddInt(
        state->bytecode,
        (int)(target - BVSize(state->bytecode) - sizeof(int)));
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

static uint16 getLocalIndex(ParseState *state, vref name)
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
                    vref filename, uint line, uint offset)
{
    const ParameterInfo *parameterInfo;
    uint parameterCount;
    size_t size;
    uint i;

    assert(filename);
    assert(line == 1 || line <= offset);
    FileOpen(&state->fh, HeapGetString(filename), VStringLength(filename));
    FileMMap(&state->fh, &state->start, &size);
    state->current = state->start + offset;
    state->limit = state->start + size;
    state->ns = ns;
    state->function = function;
    state->filename = filename;
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
                             HeapGetString(parameterInfo->name));
                    return;
                }
            }
        }
    }
    IVInit(&state->blockStack, 16);
}

void ParseStateDispose(ParseState *state)
{
    ParseStateCheck(state);
    FileClose(&state->fh);
    IVDispose(&state->blockStack);
    IntHashMapDispose(&state->locals);
}


static void beginBlock(ParseState *state, BlockType type)
{
    IVAdd(&state->blockStack, state->indent);
    IVAdd(&state->blockStack, type);
    state->indent = 0;
}

static void beginJumpBlock(ParseState *state, BlockType type)
{
    /* MAX_UINT - 1 doesn't produce any warning when uint == size_t. */
    assert(BVSize(state->bytecode) <= UINT_MAX - 1);
    IVAdd(&state->blockStack, (uint)BVSize(state->bytecode));
    beginBlock(state, type);
}

static void beginLoopBlock(ParseState *state, BlockType type,
                           size_t loopOffset)
{
    /* MAX_UINT - 1 doesn't produce any warning when uint == size_t. */
    assert(loopOffset <= UINT_MAX - 1);
    IVAdd(&state->blockStack, (uint)loopOffset);
    beginJumpBlock(state, type);
}

static void writeElse(ParseState *state, BlockType type)
{
    BVAdd(state->bytecode, OP_JUMP);
    beginJumpBlock(state, type);
    BVAddInt(state->bytecode, 0);
}

boolean ParseStateFinishBlock(ParseState *restrict state,
                              uint indent, boolean trailingElse)
{
    uint prevIndent;
    uint jumpOffset = 0;
    BlockType type;
    uint loopOffset;

    ParseStateCheck(state);

    if (!IVSize(&state->blockStack))
    {
        state->indent = 0;

        if (indent)
        {
            setError(state, "Mismatched indentation level.");
            return false;
        }

        FunctionIndexSetLocals(state->function, &state->locals,
                               getLocalsCount(state));
        ParseStateWriteInstruction(state, OP_RETURN_VOID);
        return true;
    }

    type = (BlockType)(int)IVPop(&state->blockStack);
    prevIndent = IVPop(&state->blockStack);
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

        jumpOffset = IVPop(&state->blockStack);
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
            jumpOffset = IVPop(&state->blockStack);
            break;

        case BLOCK_CONDITION1:
            jumpOffset = IVPop(&state->blockStack);
            writeElse(state, BLOCK_CONDITION2);
            break;

        case BLOCK_CONDITION2:
            jumpOffset = IVPop(&state->blockStack);
            break;

        case BLOCK_WHILE:
            jumpOffset = IVPop(&state->blockStack);
            loopOffset = IVPop(&state->blockStack);
            writeBackwardsJump(state, loopOffset);
            break;
        }
    }

    if (jumpOffset)
    {
        BVSetInt(
            state->bytecode, jumpOffset,
            (int)(BVSize(state->bytecode) - jumpOffset - sizeof(int)));
    }
    return true;
}

size_t ParseStateGetJumpTarget(ParseState *state)
{
    ParseStateCheck(state);
    return BVSize(state->bytecode);
}

void ParseStateBeginForwardJump(ParseState *state, Instruction instruction,
                                size_t *branch)
{
    ParseStateWriteInstruction(state, instruction);
    *branch = BVSize(state->bytecode);
    BVAddUint(state->bytecode, 0);
}

void ParseStateFinishJump(ParseState *state, size_t branch)
{
    ParseStateCheck(state);
    BVSetUint(
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


boolean ParseStateIsParameter(ParseState *state, vref name)
{
    uint local = IntHashMapGet(&state->locals, uintFromRef(name));
    if (!local)
    {
        return false;
    }
    return local <= FunctionIndexGetParameterCount(state->function);
}

int ParseStateGetVariableIndex(ParseState *state, vref name)
{
    uint16 local = getLocalIndex(state, name);
    if (local == UINT16_MAX)
    {
        return -1;
    }
    return local;
}

boolean ParseStateGetVariable(ParseState *state, vref name)
{
    uint16 local = getLocalIndex(state, name);
    if (local == UINT16_MAX)
    {
        return false;
    }
    ParseStateGetUnnamedVariable(state, local);
    return true;
}

boolean ParseStateSetVariable(ParseState *state, vref name)
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
    BVAdd(state->bytecode, OP_LOAD);
    BVAddUint16(state->bytecode, variable);
}

void ParseStateSetUnnamedVariable(ParseState *state, uint16 variable)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_STORE);
    BVAddUint16(state->bytecode, variable);
}


void ParseStateGetField(ParseState *state, fieldref field)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_LOAD_FIELD);
    BVAddUint(state->bytecode, FieldIndexGetIndex(field));
}

void ParseStateSetField(ParseState *state, fieldref field)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_STORE_FIELD);
    BVAddUint(state->bytecode, FieldIndexGetIndex(field));
}


void ParseStateWriteInstruction(ParseState *state, Instruction instruction)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, instruction);
}

void ParseStateWritePush(ParseState *state, vref value)
{
    ParseStateCheck(state);
    if (!value)
    {
        BVAdd(state->bytecode, OP_NULL);
    }
    else if (value == HeapTrue)
    {
        BVAdd(state->bytecode, OP_TRUE);
    }
    else if (value == HeapFalse)
    {
        BVAdd(state->bytecode, OP_FALSE);
    }
    else if (value == HeapEmptyList)
    {
        BVAdd(state->bytecode, OP_EMPTY_LIST);
    }
    else
    {
        BVAdd(state->bytecode, OP_PUSH);
        BVAddUint(state->bytecode, uintFromRef(value));
    }
}

void ParseStateReorderStack(ParseState *state, const uint16 *reorder,
                            uint16 count)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_REORDER_STACK);
    BVAddUint16(state->bytecode, count);
    while (count--)
    {
        BVAddUint16(state->bytecode, *reorder++);
    }
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
    BVAddUint(state->bytecode, size);
}

void ParseStateWriteFilelist(ParseState *state, vref pattern)
{
    ParseStateCheck(state);
    ParseStateWriteInstruction(state, OP_FILELIST);
    BVAddRef(state->bytecode, pattern);
}

void ParseStateWriteBeginCondition(ParseState *state)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_BRANCH_FALSE);
    beginJumpBlock(state, BLOCK_CONDITION1);
    BVAddInt(state->bytecode, 0);
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
    BVAdd(state->bytecode, OP_BRANCH_FALSE);
    beginJumpBlock(state, BLOCK_IF);
    BVAddInt(state->bytecode, 0);
}

void ParseStateWriteWhile(ParseState *state, size_t loopTarget)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_BRANCH_FALSE);
    beginLoopBlock(state, BLOCK_WHILE, loopTarget);
    BVAddInt(state->bytecode, 0);
}

void ParseStateWriteReturn(ParseState *state, uint values)
{
    assert(values);
    assert(values <= UINT8_MAX); /* TODO: report error */
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_RETURN);
    BVAdd(state->bytecode, (uint8)values);
}

void ParseStateWriteInvocation(ParseState *state, functionref function,
                               uint returnValues)
{
    assert(returnValues <= UINT8_MAX); /* TODO: report error */
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_INVOKE);
    BVAddRef(state->bytecode, function);
    BVAdd(state->bytecode, (uint8)returnValues);
}

void ParseStateWriteNativeInvocation(ParseState *state,
                                     nativefunctionref function)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, OP_INVOKE_NATIVE);
    BVAdd(state->bytecode, (byte)uintFromRef(function));
}
