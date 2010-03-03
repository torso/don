#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "native.h"
#include "parsestate.h"
#include "instruction.h"
#include "log.h"

#define LOCAL_OFFSET_IDENTIFIER 0
#define LOCAL_OFFSET_VALUE 1
#define LOCAL_OFFSET_FLAGS 2
#define LOCAL_ENTRY_SIZE 4

#define LOCAL_FLAG_MODIFIED 1
#define LOCAL_FLAG_ACCESSED 2

void ParseStateCheck(const ParseState *state)
{
    assert(state->start);
    assert(state->current >= state->start);
    assert(state->current <= state->start + FileIndexGetSize(state->file));
}

static void error(ParseState *state, const char *message)
{
    ParseStateSetFailed(state);
    LogParseError(state->file, state->line, message);
}

static Function *getFunction(const ParseState *state)
{
    return state->currentFunction;
}

static Block *getBlock(const ParseState *state)
{
    return getFunction(state)->currentBlock;
}

static intvector *getLocals(const ParseState *state)
{
    return &getBlock(state)->locals;
}

static bytevector *getData(const ParseState *state)
{
    return &getFunction(state)->data;
}

static bytevector *getControl(const ParseState *state)
{
    return &getFunction(state)->control;
}


static boolean writeParsed(const ParseState *restrict state,
                           bytevector *restrict parsed)
{
    bytevector *data = getData(state);
    bytevector *control = getControl(state);
    if (!ByteVectorAddPackUint(parsed, ByteVectorSize(data)))
    {
        return false;
    }
    ByteVectorAppendAll(data, parsed);
    if (!ByteVectorAddPackUint(parsed, ByteVectorSize(control) + 1))
    {
        return false;
    }
    ByteVectorAppendAll(control, parsed);
    return ByteVectorAdd(parsed, OP_RETURN);
}


static void initBlock(Block *block)
{
    block->parent = null;
    IntVectorInit(&block->locals);
    block->indent = 0;
}

static Block *createBlock(ParseState *state, Block *unfinished)
{
    Block *block = (Block*)malloc(sizeof(Block));
    intvector *locals;
    intvector *oldLocals;
    intvector *unfinishedLocals;
    uint i;

    if (!block)
    {
        return null;
    }
    initBlock(block);
    block->parent = getBlock(state);
    block->unfinished = unfinished;
    state->currentFunction->currentBlock = block;

    locals = getLocals(state);
    if (getBlock(state)->parent)
    {
        oldLocals = &getBlock(state)->parent->locals;
        for (i = 0; i < IntVectorSize(oldLocals); i += LOCAL_ENTRY_SIZE)
        {
            IntVectorAdd4(locals,
                          IntVectorGet(oldLocals, i + LOCAL_OFFSET_IDENTIFIER),
                          IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE),
                          0, 0);
        }
        if (unfinished)
        {
            unfinishedLocals = &unfinished->locals;
            for (i = IntVectorSize(oldLocals);
                 i < IntVectorSize(unfinishedLocals);
                 i += LOCAL_ENTRY_SIZE)
            {
                IntVectorAdd4(
                    locals,
                    IntVectorGet(unfinishedLocals, i + LOCAL_OFFSET_IDENTIFIER),
                    (int)getFunction(state)->valueCount++, 0, 0);
                ByteVectorAdd(getData(state), DATAOP_NULL);
            }
        }
    }
    else
    {
        assert(!unfinished);
    }
    return block;
}

static void disposeBlock(Block *block)
{
    IntVectorFree(&block->locals);
    if (block->parent)
    {
        free(block);
    }
}

static void disposeCurrentBlock(ParseState *state)
{
    Block *block = getBlock(state);
    getFunction(state)->currentBlock = block->parent;
    disposeBlock(block);
}

static void initFunction(ParseState *state, Function *function)
{
    function->parent = state->currentFunction;
    ByteVectorInit(&function->data);
    ByteVectorInit(&function->control);
    function->valueCount = 0;
    function->parameterCount = 0;
    state->currentFunction = function;
    function->currentBlock = &function->firstBlock;
    initBlock(&function->firstBlock);
}

static void disposeCurrentFunction(ParseState *state)
{
    Function *function = getFunction(state);
    while (function->currentBlock)
    {
        disposeCurrentBlock(state);
    }
    ByteVectorFree(&function->data);
    ByteVectorFree(&function->control);
    state->currentFunction = function->parent;
    if (function->parent)
    {
        free(function);
    }
}


static uint getLocalIndex(const intvector *locals, stringref name)
{
    uint i;
    uint size = IntVectorSize(locals);
    for (i = 0; i < size; i += LOCAL_ENTRY_SIZE)
    {
        if ((stringref)IntVectorGet(locals, i) == name)
        {
            return i;
        }
    }
    return size;
}

static uint getLocal(ParseState *state, Function *function, stringref name)
{
    uint value;
    uint index;
    intvector *locals;
    boolean success;

    ParseStateCheck(state);
    assert(function);
    assert(function->currentBlock);

    locals = &function->currentBlock->locals;
    index = getLocalIndex(locals, name);
    if (index < IntVectorSize(locals))
    {
        return (uint)IntVectorGet(locals, index + LOCAL_OFFSET_VALUE);
    }
    value = function->valueCount++;
    IntVectorAdd4(locals, (int)name, (int)value, LOCAL_FLAG_ACCESSED, 0);
    if (!function->parent)
    {
        success = ByteVectorAdd(&function->data, DATAOP_NULL);
        assert(success); /* TODO: Error handling */
        return value;
    }
    function->parameterCount++;
    if (!ByteVectorAdd(&function->data, DATAOP_PARAMETER) ||
        !ByteVectorAddPackInt(&function->data, (int)name))
    {
        ParseStateSetFailed(state);
    }
    return value;
}


void ParseStateInit(ParseState *state, fileref file, uint line, uint offset)
{
    assert(file);
    assert(line == 1 || line <= offset);
    state->start = FileIndexGetContents(file);
    state->current = state->start + offset;
    state->file = file;
    state->line = line;
    state->failed = false;
    state->currentFunction = null;
    initFunction(state, &state->firstFunction);
}

void ParseStateDispose(ParseState *state)
{
    while (getFunction(state))
    {
        disposeCurrentFunction(state);
    }
}

void ParseStateSetFailed(ParseState *state)
{
    ParseStateCheck(state);
    state->failed = true;
}


static boolean finishIfBlockNoElse(ParseState *state)
{
    Block *block = getBlock(state);
    intvector *locals;
    intvector *oldLocals;
    uint i;
    int flags;
    int oldFlags;

    ParseStateCheck(state);
    assert(getBlock(state));
    assert(getBlock(state)->parent);

    locals = getLocals(state);
    oldLocals = &getBlock(state)->parent->locals;

    assert(IntVectorSize(locals) >= IntVectorSize(oldLocals));
    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
        if (i >= IntVectorSize(oldLocals))
        {
            IntVectorAdd4(oldLocals,
                          IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                          (int)getFunction(state)->valueCount++, 0, 0);
            if (!ByteVectorAdd(getData(state), DATAOP_NULL))
            {
                return false;
            }
        }
        oldFlags = IntVectorGet(oldLocals, i + LOCAL_OFFSET_FLAGS);
        if (flags & LOCAL_FLAG_MODIFIED)
        {
            if (!ByteVectorAdd(getData(state), DATAOP_PHI_VARIABLE) ||
                !ByteVectorAddUint(getData(state), block->condition) ||
                !ByteVectorAddUint(
                    getData(state),
                    (uint)IntVectorGet(oldLocals,
                                       i + LOCAL_OFFSET_VALUE)) ||
                !ByteVectorAddUint(
                    getData(state),
                    (uint)IntVectorGet(locals, i + LOCAL_OFFSET_VALUE)))
            {
                return false;
            }
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE,
                         (int)getFunction(state)->valueCount++);
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS,
                         oldFlags | LOCAL_FLAG_MODIFIED);
        }
    }
    ByteVectorSetUint(getControl(state), block->branchOffset,
                      ByteVectorSize(getControl(state)));
    disposeCurrentBlock(state);
    return true;
}

static boolean finishIfBlockWithElse(ParseState *state)
{
    Block *block = getBlock(state);
    intvector *locals;
    intvector *oldLocals;
    intvector *unfinishedLocals;
    uint i;
    int flags;
    int oldFlags;

    ParseStateCheck(state);
    assert(getBlock(state));
    assert(getBlock(state)->parent);

    locals = getLocals(state);
    oldLocals = &getBlock(state)->parent->locals;
    unfinishedLocals = &getBlock(state)->unfinished->locals;

    assert(IntVectorSize(locals) >= IntVectorSize(unfinishedLocals));
    assert(IntVectorSize(unfinishedLocals) >= IntVectorSize(oldLocals));
    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
        if (i >= IntVectorSize(oldLocals))
        {
            IntVectorAdd4(oldLocals,
                          IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                          (int)getFunction(state)->valueCount++, 0, 0);
            if (!ByteVectorAdd(getData(state), DATAOP_NULL))
            {
                return false;
            }
        }
        oldFlags = IntVectorGet(oldLocals, i + LOCAL_OFFSET_FLAGS);
        if (IntVectorSize(unfinishedLocals) > i)
        {
            flags |= IntVectorGet(unfinishedLocals, i + LOCAL_OFFSET_FLAGS);
        }
        if (flags & LOCAL_FLAG_MODIFIED)
        {
            if (!ByteVectorAdd(getData(state), DATAOP_PHI_VARIABLE) ||
                !ByteVectorAddUint(getData(state),
                                   block->unfinished->condition) ||
                !ByteVectorAddUint(
                    getData(state),
                    (uint)IntVectorGet(locals, i + LOCAL_OFFSET_VALUE)) ||
                !ByteVectorAddUint(
                    getData(state),
                    (uint)IntVectorGet(IntVectorSize(unfinishedLocals) > i ?
                                       unfinishedLocals : oldLocals,
                                       i + LOCAL_OFFSET_VALUE)))
            {
                return false;
            }
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE,
                         (int)getFunction(state)->valueCount++);
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS,
                         oldFlags | LOCAL_FLAG_MODIFIED);
        }
    }
    ByteVectorSetUint(getControl(state), block->unfinished->branchOffset,
                      ByteVectorSize(getControl(state)));
    disposeBlock(block->unfinished);
    disposeCurrentBlock(state);
    return true;
}

static boolean finishLoopBlock(ParseState *restrict state,
                               bytevector *restrict parsed)
{
    Function *function = getFunction(state);
    intvector *locals;
    intvector *oldLocals;
    bytevector *parentData;
    bytevector *parentControl;
    stringref name;
    uint i;
    uint index;
    int flags;
    uint value;
    uint oldValue;

    ParseStateCheck(state);
    assert(getBlock(state));
    assert(function->parent);

    locals = getLocals(state);
    oldLocals = &function->parent->currentBlock->locals;
    parentData = &function->parent->data;
    parentControl = &function->parent->control;

    if (!ByteVectorAddPackUint(parentControl, ByteVectorSize(parsed)) ||
        !ByteVectorAddPackUint(parentControl, function->parameterCount) ||
        !writeParsed(state, parsed))
    {
        return false;
    }

    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        name = (stringref)IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER);
        flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
        index = getLocalIndex(oldLocals, name);
        if (index == IntVectorSize(oldLocals))
        {
            getLocal(state, function->parent, name);
        }
        oldValue = (uint)IntVectorGet(oldLocals, index + LOCAL_OFFSET_VALUE);
        if (flags & LOCAL_FLAG_ACCESSED)
        {
            if (!ByteVectorAddPackUint(parentControl, oldValue))
            {
                return false;
            }
        }
        if (flags & LOCAL_FLAG_MODIFIED)
        {
            value = function->parent->valueCount++;
            if (!ByteVectorAdd(&function->parent->data, DATAOP_RETURN) ||
                !ByteVectorAddPackUint(&function->parent->data,
                                       function->stackframe) ||
                !ByteVectorAddPackUint(
                    &function->parent->data,
                    (uint)IntVectorGet(locals, i + LOCAL_OFFSET_VALUE)))
            {
                return false;
            }
            IntVectorSet(oldLocals, index + LOCAL_OFFSET_VALUE,
                         (int)function->parent->valueCount++);
            if (!ByteVectorAdd(&function->parent->data, DATAOP_PHI_VARIABLE) ||
                !ByteVectorAddUint(&function->parent->data,
                                   function->firstBlock.condition) ||
                !ByteVectorAddUint(&function->parent->data, oldValue) ||
                !ByteVectorAddUint(&function->parent->data, value))
            {
                return false;
            }
        }
    }
    disposeCurrentFunction(state);
    return true;
}

boolean ParseStateFinishBlock(ParseState *restrict state,
                              bytevector *restrict parsed,
                              uint indent, boolean trailingElse)
{
    Function *function = getFunction(state);
    Function *parentFunction;
    Block *block = function->currentBlock;
    Block *parentBlock = block->parent;
    Block *newBlock;
    uint conditionBranchOffset;

    ParseStateCheck(state);

    if (parentBlock)
    {
        if (indent > parentBlock->indent)
        {
            error(state, "Mismatched indentation level.");
            return false;
        }

        if (trailingElse && indent == parentBlock->indent)
        {
            conditionBranchOffset = block->branchOffset;
            block->branchOffset = ByteVectorSize(getControl(state)) + 1;
            if (!ByteVectorAdd(getControl(state), OP_JUMP) ||
                !ByteVectorAddInt(getControl(state), 0))
            {
                return false;
            }
            ByteVectorSetUint(getControl(state), conditionBranchOffset,
                              ByteVectorSize(getControl(state)));

            function->currentBlock = parentBlock;
            newBlock = createBlock(state, block);
            if (!newBlock)
            {
                return false;
            }
            return true;
        }
        return (boolean)(block->unfinished ?
                         finishIfBlockWithElse(state) :
                         finishIfBlockNoElse(state));
    }

    parentFunction = getFunction(state)->parent;
    if (parentFunction)
    {
        if (indent > parentFunction->currentBlock->indent)
        {
            error(state, "Mismatched indentation level.");
            return false;
        }
        if (trailingElse && indent == parentFunction->currentBlock->indent)
        {
            error(state, "Else without matching if.");
            return false;
        }
        return finishLoopBlock(state, parsed);
    }

    if (indent)
    {
        error(state, "Mismatched indentation level.");
        return false;
    }

    disposeCurrentBlock(state);
    state->parsedOffset = ByteVectorSize(parsed);
    return writeParsed(state, parsed);
}


void ParseStateSetIndent(ParseState *state, uint indent)
{
    ParseStateCheck(state);
    assert(!getBlock(state)->indent);
    getBlock(state)->indent = indent;
}

uint ParseStateBlockIndent(ParseState *state)
{
    ParseStateCheck(state);
    return getBlock(state) ? getBlock(state)->indent : 0;
}


uint ParseStateGetVariable(ParseState *state, stringref name)
{
    return getLocal(state, getFunction(state), name);
}

boolean ParseStateSetVariable(ParseState *state, stringref name,
                              uint value)
{
    uint i;
    intvector *locals = getLocals(state);
    ParseStateCheck(state);
    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        if ((stringref)IntVectorGet(locals, i) == name)
        {
            IntVectorSet(locals, i + LOCAL_OFFSET_VALUE, (int)value);
            IntVectorSet(locals, i + LOCAL_OFFSET_FLAGS,
                         IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS) |
                         LOCAL_FLAG_MODIFIED);
            return true;
        }
    }
    IntVectorAdd4(locals, (int)name, (int)value, LOCAL_FLAG_MODIFIED, 0);
    return true;
}


void ParseStateSetArgument(ParseState *state, uint argumentOffset,
                           uint parameterIndex, uint value)
{
    ParseStateCheck(state);
    ByteVectorSetUint(getControl(state),
                      argumentOffset + parameterIndex * (uint)sizeof(int),
                      value);
}


uint ParseStateWriteStringLiteral(ParseState *state, stringref value)
{
    ParseStateCheck(state);
    if (!ByteVectorAdd(getData(state), DATAOP_STRING) ||
        !ByteVectorAddPackUint(getData(state), (uint)value))
    {
        ParseStateSetFailed(state);
    }
    return getFunction(state)->valueCount++;
}


boolean ParseStateWriteIf(ParseState *state, uint value)
{
    Block *block;

    ParseStateCheck(state);
    assert(getBlock(state));

    block = createBlock(state, null);
    if (!block)
    {
        return false;
    }
    block->condition = value;
    block->branchOffset = ByteVectorSize(getControl(state)) + 1;
    return ByteVectorAdd(getControl(state), OP_BRANCH) &&
        ByteVectorAddInt(getControl(state), 0) &&
        ByteVectorAddPackUint(getControl(state), value) ?
        true : false;
}

boolean ParseStateWriteWhile(ParseState *state, uint value)
{
    Function *function;
    ParseStateCheck(state);

    function = (Function*)malloc(sizeof(Function));
    function->stackframe = getFunction(state)->valueCount;
    if (!function ||
        !ByteVectorAdd(getControl(state), OP_COND_INVOKE) ||
        !ByteVectorAddPackUint(getControl(state), value) ||
        !ByteVectorAddPackUint(getControl(state),
                               getFunction(state)->valueCount++) ||
        !ByteVectorAdd(getData(state), DATAOP_STACKFRAME))
    {
        return false;
    }
    initFunction(state, function);
    getBlock(state)->condition = value;
    return true;
}

boolean ParseStateWriteReturn(ParseState *state)
{
    ParseStateCheck(state);
    return ByteVectorAdd(getControl(state), OP_RETURN);
}

uint ParseStateWriteNativeInvocation(ParseState *state,
                                     nativefunctionref nativeFunction,
                                     uint parameterCount)
{
    uint argumentOffset;
    ParseStateCheck(state);
    if (!ByteVectorAdd(getControl(state), OP_INVOKE_NATIVE) ||
        !ByteVectorAdd(getControl(state), (byte)nativeFunction) ||
        !ByteVectorAddPackUint(getControl(state),
                               getFunction(state)->valueCount++) ||
        !ByteVectorAdd(getData(state), DATAOP_STACKFRAME) ||
        !ByteVectorAddPackUint(getControl(state), parameterCount))
    {
        return 0;
    }
    argumentOffset = ByteVectorSize(getControl(state));
    ByteVectorSetSize(getControl(state),
                      argumentOffset + parameterCount * (uint)sizeof(int));
    ByteVectorFill(getControl(state), argumentOffset,
                   parameterCount * (uint)sizeof(int), 0);
    return argumentOffset;
}
