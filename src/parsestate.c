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

static uint16 getFreeLocalIndex(ParseState *state)
{
    uint count = ParseStateLocalsCount(state);
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
}

void ParseStateDispose(ParseState *state)
{
    ParseStateCheck(state);
    FileClose(&state->fh);
    IntHashMapDispose(&state->locals);
}

uint ParseStateLocalsCount(ParseState *state)
{
    ParseStateCheck(state);
    return (uint)(IntHashMapSize(&state->locals) + state->unnamedVariables);
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

size_t ParseStateGetJumpTarget(ParseState *state)
{
    ParseStateCheck(state);
    return BVSize(state->bytecode);
}

size_t ParseStateWriteForwardJump(ParseState *state, Instruction instruction)
{
    ParseStateWriteInstruction(state, instruction);
    BVAddInt(state->bytecode, 0);
    return BVSize(state->bytecode);
}

void ParseStateFinishJump(ParseState *state, size_t branch)
{
    ParseStateCheck(state);
    BVSetUint(state->bytecode, branch - sizeof(int),
              (uint)(ParseStateGetJumpTarget(state) - branch));
}

void ParseStateWriteBackwardJump(ParseState *state, Instruction instruction, size_t target)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, instruction);
    BVAddInt(state->bytecode, (int)(target - BVSize(state->bytecode) - sizeof(int)));
}

size_t ParseStateWriteJump(ParseState *state, Instruction instruction, int offset)
{
    ParseStateCheck(state);
    BVAdd(state->bytecode, instruction);
    BVAddInt(state->bytecode, offset);
    return BVSize(state->bytecode);
}

int ParseStateSetJumpOffset(ParseState *state, size_t instructionOffset, int offset)
{
    int old = BVGetInt(state->bytecode, instructionOffset - sizeof(int));
    BVSetInt(state->bytecode, instructionOffset - sizeof(int), offset);
    return old;
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
