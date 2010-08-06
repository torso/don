#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "interpreterstate.h"
#include "native.h"
#include "targetindex.h"
#include "instruction.h"
#include "parsestate.h"
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

static void setError(ParseState *state, const char *message)
{
    ParseStateSetFailed(state, BUILD_ERROR);
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


static boolean writeParsed(ParseState *restrict state,
                           bytevector *restrict parsed)
{
    bytevector *data = getData(state);
    bytevector *control = getControl(state);
    if (ParseStateSetError(state, ByteVectorAddUint(parsed, 0)) ||
        ParseStateSetError(state, ByteVectorAddPackUint(
                               parsed, getFunction(state)->valueCount)) ||
        ParseStateSetError(state, ByteVectorAddPackUint(
                               parsed, ByteVectorSize(data))) ||
        ParseStateSetError(state, ByteVectorAddPackUint(
                               parsed, ByteVectorSize(control) + 1)) ||
        ParseStateSetError(state, ByteVectorAppendAll(data, parsed)) ||
        ParseStateSetError(state, ByteVectorAppendAll(control, parsed)) ||
        ParseStateSetError(state, ByteVectorAdd(parsed, OP_RETURN)))
    {
        return false;
    }
    return true;
}


static ErrorCode initBlock(Block *block)
{
    block->parent = null;
    block->indent = 0;
    return IntVectorInit(&block->locals);
}

static Block *createBlock(ParseState *state, Block *unfinished)
{
    Block *block = (Block*)malloc(sizeof(Block));
    intvector *locals;
    intvector *oldLocals;
    intvector *unfinishedLocals;
    uint i;

    if (!block || ParseStateSetError(state, initBlock(block)))
    {
        return null;
    }
    block->parent = getBlock(state);
    block->unfinished = unfinished;
    state->currentFunction->currentBlock = block;

    locals = getLocals(state);
    if (getBlock(state)->parent)
    {
        oldLocals = &getBlock(state)->parent->locals;
        for (i = 0; i < IntVectorSize(oldLocals); i += LOCAL_ENTRY_SIZE)
        {
            if (ParseStateSetError(
                    state,
                    IntVectorAdd4(
                        locals,
                        IntVectorGet(oldLocals, i + LOCAL_OFFSET_IDENTIFIER),
                        IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE),
                        0, 0)))
            {
                return null;
            }
        }
        if (unfinished)
        {
            unfinishedLocals = &unfinished->locals;
            for (i = IntVectorSize(oldLocals);
                 i < IntVectorSize(unfinishedLocals);
                 i += LOCAL_ENTRY_SIZE)
            {
                if (ParseStateSetError(
                        state,
                        IntVectorAdd4(
                            locals,
                            IntVectorGet(unfinishedLocals,
                                         i + LOCAL_OFFSET_IDENTIFIER),
                            getFunction(state)->valueCount++, 0, 0)) ||
                    ParseStateSetError(
                        state, ByteVectorAdd(getData(state), DATAOP_NULL)))
                {
                    return null;
                }
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
    IntVectorDispose(&block->locals);
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
    if (ParseStateSetError(state, ByteVectorInit(&function->data)) ||
        ParseStateSetError(state, ByteVectorInit(&function->control)))
    {
        return;
    }
    function->valueCount = 0;
    function->parameterCount = 0;
    state->currentFunction = function;
    function->currentBlock = &function->firstBlock;
    ParseStateSetError(state, initBlock(&function->firstBlock));
}

static void disposeCurrentFunction(ParseState *state)
{
    Function *function = getFunction(state);
    while (function->currentBlock)
    {
        disposeCurrentBlock(state);
    }
    ByteVectorDispose(&function->data);
    ByteVectorDispose(&function->control);
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

    ParseStateCheck(state);
    assert(function);
    assert(function->currentBlock);

    locals = &function->currentBlock->locals;
    index = getLocalIndex(locals, name);
    if (index < IntVectorSize(locals))
    {
        return IntVectorGet(locals, index + LOCAL_OFFSET_VALUE);
    }
    value = function->valueCount++;
    if (ParseStateSetError(
            state,
            IntVectorAdd4(locals, (uint)name, value, LOCAL_FLAG_ACCESSED, 0)))
    {
        return 0;
    }
    if (!function->parent)
    {
        if (ParseStateSetError(state,
                               ByteVectorAdd(&function->data, DATAOP_NULL)))
        {
            return 0;
        }
        return value;
    }
    function->parameterCount++;
    if (ParseStateSetError(state,
                           ByteVectorAdd(&function->data, DATAOP_PARAMETER)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(&function->data, (uint)name)))
    {
        return 0;
    }
    return value;
}


ErrorCode ParseStateInit(ParseState *state, fileref file, uint line, uint offset)
{
    assert(file);
    assert(line == 1 || line <= offset);
    state->start = FileIndexGetContents(file);
    state->current = state->start + offset;
    state->file = file;
    state->line = line;
    state->error = NO_ERROR;
    state->currentFunction = null;
    initFunction(state, &state->firstFunction);
    return state->error;
}

void ParseStateDispose(ParseState *state)
{
    while (getFunction(state))
    {
        disposeCurrentFunction(state);
    }
}

boolean ParseStateSetError(ParseState *state, ErrorCode error)
{
    ParseStateCheck(state);
    state->error = error;
    return state->error ? true : false;
}

void ParseStateSetFailed(ParseState *state, ErrorCode error)
{
    assert(error);
    ParseStateCheck(state);
    state->error = error;
}


static boolean finishIfBlockNoElse(ParseState *state)
{
    Block *block = getBlock(state);
    intvector *locals;
    intvector *oldLocals;
    uint i;
    uint flags;
    uint oldFlags;

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
            if (ParseStateSetError(
                    state,
                    IntVectorAdd4(
                        oldLocals,
                        IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                        getFunction(state)->valueCount++, 0, 0)) ||
                ParseStateSetError(state,
                                   ByteVectorAdd(getData(state), DATAOP_NULL)))
            {
                return false;
            }
        }
        oldFlags = IntVectorGet(oldLocals, i + LOCAL_OFFSET_FLAGS);
        if (flags & LOCAL_FLAG_MODIFIED)
        {
            if (ParseStateSetError(
                    state, ByteVectorAdd(getData(state), DATAOP_CONDITION)) ||
                ParseStateSetError(
                    state,
                    ByteVectorAddUint(getData(state), block->condition)) ||
                ParseStateSetError(
                    state,
                    ByteVectorAddUint(
                        getData(state),
                        IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE))) ||
                ParseStateSetError(
                    state,
                    ByteVectorAddUint(
                        getData(state),
                        IntVectorGet(locals, i + LOCAL_OFFSET_VALUE))))
            {
                return false;
            }
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE,
                         getFunction(state)->valueCount++);
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS,
                         oldFlags | LOCAL_FLAG_MODIFIED);
        }
    }
    ByteVectorSetUint(getControl(state), block->branchOffset,
                      ByteVectorSize(getControl(state)) -
                      block->branchOffset - 4);
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
    uint flags;
    uint oldFlags;

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
            if (ParseStateSetError(
                    state,
                    IntVectorAdd4(
                        oldLocals,
                        IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                        getFunction(state)->valueCount++, 0, 0)) ||
                ParseStateSetError(state,
                                   ByteVectorAdd(getData(state), DATAOP_NULL)))
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
            if (ParseStateSetError(
                    state, ByteVectorAdd(getData(state), DATAOP_CONDITION)) ||
                ParseStateSetError(
                    state, ByteVectorAddUint(getData(state),
                                             block->unfinished->condition)) ||
                ParseStateSetError(
                    state, ByteVectorAddUint(
                        getData(state),
                        IntVectorGet(locals, i + LOCAL_OFFSET_VALUE))) ||
                ParseStateSetError(
                    state, ByteVectorAddUint(
                        getData(state),
                        IntVectorGet(IntVectorSize(unfinishedLocals) > i ?
                                     unfinishedLocals : oldLocals,
                                     i + LOCAL_OFFSET_VALUE))))
            {
                return false;
            }
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE,
                         getFunction(state)->valueCount++);
            IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS,
                         oldFlags | LOCAL_FLAG_MODIFIED);
        }
    }
    ByteVectorSetUint(getControl(state), block->unfinished->branchOffset,
                      ByteVectorSize(getControl(state)) -
                      block->unfinished->branchOffset - 4);
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
    uint flags;
    uint value;
    uint oldValue;

    ParseStateCheck(state);
    assert(getBlock(state));
    assert(function->parent);

    locals = getLocals(state);
    oldLocals = &function->parent->currentBlock->locals;
    parentData = &function->parent->data;
    parentControl = &function->parent->control;

    if (ParseStateSetError(
            state, ByteVectorAddPackUint(parentData, ByteVectorSize(parsed))) ||
        ParseStateSetError(
            state,
            ByteVectorAddPackUint(parentControl, function->parameterCount)) ||
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
            if (state->error)
            {
                return false;
            }
        }
        oldValue = IntVectorGet(oldLocals, index + LOCAL_OFFSET_VALUE);
        if (flags & LOCAL_FLAG_ACCESSED)
        {
            if (ParseStateSetError(
                    state, ByteVectorAddPackUint(parentControl, oldValue)))
            {
                return false;
            }
        }
        if (flags & LOCAL_FLAG_MODIFIED)
        {
            value = function->parent->valueCount++;
            if (ParseStateSetError(
                    state,
                    ByteVectorAdd(&function->parent->data, DATAOP_RETURN)) ||
                ParseStateSetError(
                    state, ByteVectorAddPackUint(&function->parent->data,
                                                 function->stackframe)) ||
                ParseStateSetError(
                    state, ByteVectorAddPackUint(
                        &function->parent->data,
                        IntVectorGet(locals, i + LOCAL_OFFSET_VALUE))))
            {
                return false;
            }
            IntVectorSet(oldLocals, index + LOCAL_OFFSET_VALUE,
                         function->parent->valueCount++);
            if (ParseStateSetError(
                    state,
                    ByteVectorAdd(&function->parent->data, DATAOP_CONDITION)) ||
                ParseStateSetError(
                    state, ByteVectorAddUint(&function->parent->data,
                                             function->firstBlock.condition)) ||
                ParseStateSetError(
                    state,
                    ByteVectorAddUint(&function->parent->data, oldValue)) ||
                ParseStateSetError(
                    state, ByteVectorAddUint(&function->parent->data, value)))
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
            setError(state, "Mismatched indentation level.");
            return false;
        }

        if (trailingElse && indent == parentBlock->indent)
        {
            conditionBranchOffset = block->branchOffset;
            block->branchOffset = ByteVectorSize(getControl(state)) + 1;
            if (ParseStateSetError(state,
                                   ByteVectorAdd(getControl(state), OP_JUMP)) ||
                ParseStateSetError(state,
                                   ByteVectorAddInt(getControl(state), 0)))
            {
                return false;
            }
            ByteVectorSetUint(getControl(state), conditionBranchOffset,
                              ByteVectorSize(getControl(state)) -
                              conditionBranchOffset - 4);

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
            setError(state, "Mismatched indentation level.");
            return false;
        }
        if (trailingElse && indent == parentFunction->currentBlock->indent)
        {
            setError(state, "Else without matching if.");
            return false;
        }
        return finishLoopBlock(state, parsed);
    }

    if (indent)
    {
        setError(state, "Mismatched indentation level.");
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

boolean ParseStateSetVariable(ParseState *state, stringref name, uint value)
{
    uint i;
    intvector *locals = getLocals(state);
    ParseStateCheck(state);
    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        if ((stringref)IntVectorGet(locals, i) == name)
        {
            IntVectorSet(locals, i + LOCAL_OFFSET_VALUE, value);
            IntVectorSet(locals, i + LOCAL_OFFSET_FLAGS,
                         IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS) |
                         LOCAL_FLAG_MODIFIED);
            return true;
        }
    }
    if (ParseStateSetError(
            state,
            IntVectorAdd4(locals, (uint)name, value, LOCAL_FLAG_MODIFIED, 0)))
    {
        ParseStateSetFailed(state, OUT_OF_MEMORY);
        return false;
    }
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


uint ParseStateWriteNullLiteral(ParseState *state)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state, ByteVectorAdd(getData(state), DATAOP_NULL)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteTrueLiteral(ParseState *state)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state, ByteVectorAdd(getData(state), DATAOP_TRUE)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteFalseLiteral(ParseState *state)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state, ByteVectorAdd(getData(state), DATAOP_FALSE)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteIntegerLiteral(ParseState *state, int value)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state,
                           ByteVectorAdd(getData(state), DATAOP_INTEGER)) ||
        ParseStateSetError(state, ByteVectorAddPackInt(getData(state), value)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteStringLiteral(ParseState *state, stringref value)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state,
                           ByteVectorAdd(getData(state), DATAOP_STRING)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(getData(state), (uint)value)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteList(ParseState *state, const intvector *values)
{
    uint i;
    uint count;

    ParseStateCheck(state);
    count = IntVectorSize(values);
    if (ParseStateSetError(state, ByteVectorAdd(getData(state), DATAOP_LIST)) ||
        ParseStateSetError(state, ByteVectorAddPackUint(getData(state), count)))
    {
        return 0;
    }
    for (i = 0; i < count; i++)
    {
        if (ParseStateSetError(
                state,
                ByteVectorAddPackUint(getData(state), IntVectorGet(values, i))))
        {
            return 0;
        }
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteBinaryOperation(ParseState *state,
                                    DataInstruction operation,
                                    uint value1, uint value2)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state, ByteVectorAdd(getData(state), operation)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(getData(state), value1)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(getData(state), value2)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

uint ParseStateWriteTernaryOperation(ParseState *state,
                                     DataInstruction operation,
                                     uint value1, uint value2, uint value3)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state, ByteVectorAdd(getData(state), operation)) ||
        ParseStateSetError(state, ByteVectorAddUint(getData(state), value1)) ||
        ParseStateSetError(state, ByteVectorAddUint(getData(state), value2)) ||
        ParseStateSetError(state, ByteVectorAddUint(getData(state), value3)))
    {
        return 0;
    }
    return getFunction(state)->valueCount++;
}

boolean ParseStateWriteIf(ParseState *state, uint value)
{
    Block *block;

    ParseStateCheck(state);
    assert(getBlock(state));

    block = createBlock(state, null);
    if (!block ||
        ParseStateSetError(state,
                           ByteVectorAdd(getControl(state), OP_BRANCH)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(getControl(state), value)))
    {
        return false;
    }
    block->condition = value;
    block->branchOffset = ByteVectorSize(getControl(state));
    if (ParseStateSetError(state, ByteVectorAddInt(getControl(state), 0)))
    {
        return false;
    }
    return true;
}

boolean ParseStateWriteWhile(ParseState *state, uint value)
{
    Function *function;
    ParseStateCheck(state);

    function = (Function*)malloc(sizeof(Function));
    function->stackframe = getFunction(state)->valueCount;
    if (!function ||
        ParseStateSetError(state,
                           ByteVectorAdd(getControl(state), OP_COND_INVOKE)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(getControl(state), value)) ||
        ParseStateSetError(state,
                           ByteVectorAddPackUint(
                               getControl(state),
                               getFunction(state)->valueCount++)) ||
        ParseStateSetError(state,
                           ByteVectorAdd(
                               getData(state), DATAOP_STACKFRAME_ABSOLUTE)))
    {
        return false;
    }
    initFunction(state, function);
    if (state->error)
    {
        return false;
    }
    getBlock(state)->condition = value;
    return true;
}

boolean ParseStateWriteReturn(ParseState *state)
{
    ParseStateCheck(state);
    if (ParseStateSetError(state, ByteVectorAdd(getControl(state), OP_RETURN)))
    {
        return false;
    }
    return true;
}

uint ParseStateWriteInvocation(ParseState *state,
                               nativefunctionref nativeFunction,
                               targetref target,
                               uint parameterCount,
                               uint *arguments)
{
    uint returnValue;

    ParseStateCheck(state);
    returnValue = getFunction(state)->valueCount++;
    if (nativeFunction >= 0)
    {
        assert(!target);
        if (ParseStateSetError(
                state, ByteVectorAdd(getControl(state), OP_INVOKE_NATIVE)) ||
            ParseStateSetError(
                state, ByteVectorAdd(getControl(state), (byte)nativeFunction)))
        {
            return 0;
        }
    }
    else
    {
        if (ParseStateSetError(
                state, ByteVectorAdd(getControl(state), OP_INVOKE_TARGET)) ||
            ParseStateSetError(
                state, ByteVectorAddPackUint(getControl(state), target)))
        {
            return 0;
        }
    }
    if (ParseStateSetError(
            state, ByteVectorAddPackUint(getControl(state), returnValue)) ||
        ParseStateSetError(
            state, ByteVectorAdd(getData(state), DATAOP_STACKFRAME)) ||
        ParseStateSetError(
            state, ByteVectorAddPackUint(getControl(state), parameterCount)))
    {

        return 0;
    }
    while (parameterCount--)
    {
        if (ParseStateSetError(
                state, ByteVectorAddPackUint(getControl(state), *arguments++)))
        {
            return 0;
        }
    }
    return returnValue;
}
