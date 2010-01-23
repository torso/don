#include <stdlib.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "native.h"
#include "parsestate.h"
#include "instruction.h"

#define LOCAL_OFFSET_IDENTIFIER 0
#define LOCAL_OFFSET_VALUE 1
#define LOCAL_OFFSET_FLAGS 2
#define LOCAL_OFFSET_ACCESSOFFSET 3
#define LOCAL_ENTRY_SIZE 4

#define LOCAL_FLAG_MODIFIED 1
#define LOCAL_FLAG_ACCESSED 2

static void dump(const ParseState *state)
{
    uint ip;
    uint data;
    uint readIndex;
    uint i;
    uint function;
    uint argumentCount;
    int condition;
    uint target;

    printf("Dump pass 1\n");
    printf("data, size=%d\n", ByteVectorSize(&state->data));
    for (readIndex = 0; readIndex < ByteVectorSize(&state->data);)
    {
        ip = readIndex;
        switch (ByteVectorRead(&state->data, &readIndex))
        {
        case DATAOP_NULL:
            printf("%d: null\n", ip);
            break;
        case DATAOP_STRING:
            data = ByteVectorReadPackUint(&state->data, &readIndex);
            printf("%d: string %d:\"%s\"\n", ip, data, StringPoolGetString((stringref)data));
            break;
        case DATAOP_PHI_VARIABLE:
            printf("%d: phi variable condition=%d %d %d\n", ip,
                   ByteVectorReadInt(&state->data, &readIndex),
                   ByteVectorReadInt(&state->data, &readIndex),
                   ByteVectorReadInt(&state->data, &readIndex));
            break;
        default:
            assert(false);
            break;
        }
    }
    printf("control, size=%d\n", ByteVectorSize(&state->control));
    for (readIndex = 0; readIndex < ByteVectorSize(&state->control);)
    {
        ip = readIndex;
        switch (ByteVectorRead(&state->control, &readIndex))
        {
        case OP_RETURN:
            printf("%d: return\n", ip);
            break;
        case OP_INVOKE_NATIVE:
            function = ByteVectorRead(&state->control, &readIndex);
            argumentCount = ByteVectorReadPackUint(&state->control, &readIndex);
            printf("%d: invoke native function=%d, arguments=%d\n", ip, function, argumentCount);
            for (i = 0; i < argumentCount; i++)
            {
                printf("  %d: argument %d\n", i, ByteVectorReadInt(&state->control, &readIndex));
            }
            break;
        case OP_BRANCH:
            target = ByteVectorReadUint(&state->control, &readIndex);
            condition = ByteVectorReadPackInt(&state->control, &readIndex);
            printf("%d: branch condition=%d target=%d\n", ip, condition, target);
            break;
        case OP_LOOP:
            target = ByteVectorReadPackUint(&state->control, &readIndex);
            printf("%d: loop %d\n", ip, target);
            break;
        case OP_JUMP:
            target = ByteVectorReadUint(&state->control, &readIndex);
            printf("%d: jump %d\n", ip, target);
            break;
        default:
            assert(false);
            break;
        }
    }
}

static void dump2(const ParseState *state)
{
    uint ip;
    uint readIndex;
    uint i;
    uint function;
    uint argumentCount;
    int condition;
    uint target;

    printf("\nDump pass 2\n");
    printf("control, size=%d\n", ByteVectorSize(&state->control));
    for (readIndex = 0; readIndex < ByteVectorSize(&state->control);)
    {
        ip = readIndex;
        switch (ByteVectorRead(&state->control, &readIndex))
        {
        case OP_RETURN:
            printf("%d: return\n", ip);
            break;
        case OP_INVOKE_NATIVE:
            function = ByteVectorRead(&state->control, &readIndex);
            argumentCount = ByteVectorReadPackUint(&state->control, &readIndex);
            printf("%d: invoke native function=%d, arguments=%d\n", ip, function, argumentCount);
            for (i = 0; i < argumentCount; i++)
            {
                printf("  %d: argument %d\n", i, ByteVectorReadPackInt(&state->control, &readIndex));
            }
            break;
        case OP_BRANCH:
            target = ByteVectorReadPackUint(&state->control, &readIndex);
            condition = ByteVectorReadPackInt(&state->control, &readIndex);
            printf("%d: branch condition=%d target=%d\n", ip, condition, target);
            break;
        case OP_LOOP:
            target = ByteVectorReadPackUint(&state->control, &readIndex);
            printf("%d: loop %d\n", ip, target);
            break;
        case OP_JUMP:
            target = ByteVectorReadPackUint(&state->control, &readIndex);
            printf("%d: jump %d\n", ip, target);
            break;
        default:
            assert(false);
            break;
        }
    }
}

static void freeBlock(Block *block)
{
    IntVectorFree(&block->locals);
    free(block);
}

void ParseStateCheck(const ParseState *state)
{
    assert(state->start != null);
    assert(state->current >= state->start);
    assert(state->current <= state->start + FileIndexGetSize(state->file));
}


static void addBranchTarget(ParseState *state, uint target)
{
    ParseStateCheck(state);
    IntVectorAdd(&state->branchTargets, (int)target);
    IntVectorAdd(&state->branchTargets, 0);
}

static uint getBranchTarget(const ParseState *state, uint target)
{
    uint i;
    ParseStateCheck(state);
    for (i = 0;; i += 2)
    {
        assert(i < IntVectorSize(&state->branchTargets));
        if ((uint)IntVectorGet(&state->branchTargets, i) == target)
        {
            return (uint)IntVectorGet(&state->branchTargets, i + 1);
        }
    }
}

static void setBranchTarget(ParseState *state, uint target, uint newTarget)
{
    uint i;
    ParseStateCheck(state);
    for (i = 0; i < IntVectorSize(&state->branchTargets); i += 2)
    {
        if ((uint)IntVectorGet(&state->branchTargets, i) == target)
        {
            IntVectorSet(&state->branchTargets, i + 1, (int)newTarget);
            return;
        }
    }
}

static uint fixBranchTarget(const ParseState *state, intvector *forwardBranches,
                            uint target, uint ip,
                            uint readIndex, uint writeIndex)
{
    if (target <= ip)
    {
        return getBranchTarget(state, target);
    }
    IntVectorAdd(forwardBranches, (int)writeIndex);
    IntVectorAdd(forwardBranches, (int)target);
    return target - (readIndex - writeIndex);
}


void ParseStateInit(ParseState *state, fileref file, uint line, uint offset)
{
    assert(file);
    assert(line == 1 || line <= offset);
    state->start = FileIndexGetContents(file);
    state->current = state->start + offset;
    state->file = file;
    state->line = line;
    state->loopLevel = 0;
    ByteVectorInit(&state->data);
    ByteVectorAdd(&state->data, 0);
    ByteVectorInit(&state->control);
    IntVectorInit(&state->branchTargets);
    state->currentBlock = &state->firstBlock;
    IntVectorInit(&state->firstBlock.locals);
}

void ParseStateDispose(ParseState *state)
{
    Block *block;
    Block *nextBlock;
    IntVectorFree(&state->firstBlock.locals);
    ByteVectorFree(&state->data);
    ByteVectorFree(&state->control);
    IntVectorFree(&state->branchTargets);
    for (block = state->currentBlock; block != &state->firstBlock;)
    {
        nextBlock = block->parent;
        freeBlock(block);
        block = nextBlock;
    }
}

void ParseStateFinish(ParseState *state)
{
    byte op;
    uint ip;
    uint readIndex;
    uint writeIndex;
    uint targetIndex;
    uint i;
    byte function;
    uint argumentCount;
    uint condition;
    uint target;
    intvector forwardBranches;

    dump(state);

    IntVectorInit(&forwardBranches);
    for (readIndex = 0, writeIndex = 0;
         readIndex < ByteVectorSize(&state->control);)
    {
        setBranchTarget(state, readIndex, writeIndex);
        ip = readIndex;
        op = ByteVectorRead(&state->control, &readIndex);
        switch (op)
        {
        case OP_RETURN:
            ByteVectorWrite(&state->control, &writeIndex, op);
            break;
        case OP_INVOKE_NATIVE:
            function = ByteVectorRead(&state->control, &readIndex);
            argumentCount = ByteVectorReadPackUint(&state->control, &readIndex);
            ByteVectorWrite(&state->control, &writeIndex, op);
            ByteVectorWrite(&state->control, &writeIndex, function);
            ByteVectorWritePackUint(&state->control, &writeIndex,
                                    argumentCount);
            for (i = 0; i < argumentCount; i++)
            {
                ByteVectorWritePackInt(
                    &state->control, &writeIndex,
                    ByteVectorReadInt(&state->control, &readIndex));
            }
            break;
        case OP_BRANCH:
            targetIndex = readIndex;
            target = ByteVectorReadUint(&state->control, &readIndex);
            condition = ByteVectorReadPackUint(&state->control, &readIndex);
            ByteVectorWrite(&state->control, &writeIndex, op);
            ByteVectorWritePackUint(
                &state->control, &writeIndex,
                fixBranchTarget(state, &forwardBranches, target, ip,
                                targetIndex, writeIndex));
            ByteVectorWritePackUint(&state->control, &writeIndex, condition);
            break;
        case OP_LOOP:
            targetIndex = readIndex;
            target = ByteVectorReadPackUint(&state->control, &readIndex);
            ByteVectorWrite(&state->control, &writeIndex, op);
            ByteVectorWritePackUint(
                &state->control, &writeIndex,
                fixBranchTarget(state, &forwardBranches, target, ip,
                                targetIndex, writeIndex));
            break;
        case OP_JUMP:
            targetIndex = readIndex;
            target = ByteVectorReadUint(&state->control, &readIndex);
            ByteVectorWrite(&state->control, &writeIndex, op);
            ByteVectorWritePackUint(
                &state->control, &writeIndex,
                fixBranchTarget(state, &forwardBranches, target, ip,
                                targetIndex, writeIndex));
            break;
        default:
            assert(false);
            break;
        }
        assert(writeIndex <= readIndex);
    }
    ByteVectorSetSize(&state->control, writeIndex);

    for (i = 0; i < IntVectorSize(&forwardBranches); i += 2)
    {
        writeIndex = (uint)IntVectorGet(&forwardBranches, i);
        target = (uint)IntVectorGet(&forwardBranches, i + 1);
        ByteVectorSetPackUint(&state->control, writeIndex,
                              getBranchTarget(state, target));
    }

    IntVectorFree(&forwardBranches);

    dump2(state);
}


boolean ParseStateBlockBegin(ParseState *state, uint indent, boolean loop,
                             boolean allowTrailingElse)
{
    Block *block;
    intvector *locals;
    intvector *oldLocals = &state->currentBlock->locals;
    uint i;
    ParseStateCheck(state);
    block = (Block*)malloc(sizeof(Block));
    if (block == null)
    {
        return false;
    }
    block->parent = state->currentBlock;
    block->unfinished = null;
    block->indent = indent;
    block->loopBegin = ByteVectorSize(&state->control);
    block->loop = loop;
    block->allowTrailingElse = allowTrailingElse;
    state->currentBlock = block;
    if (loop)
    {
        locals = &block->locals;
        IntVectorInit(locals);
        for (i = 0; i < IntVectorSize(oldLocals); i += LOCAL_ENTRY_SIZE)
        {
            IntVectorAdd4(locals,
                          IntVectorGet(oldLocals, i + LOCAL_OFFSET_IDENTIFIER),
                          IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE),
                          0, 0);
        }
        state->loopLevel++;
    }
    else
    {
        IntVectorInitCopy(&block->locals, &block->parent->locals);
    }
    return true;
}

static void ParseStateBlockSetCondition(ParseState *state, int value)
{
    assert(!ParseStateBlockEmpty(state));
    state->currentBlock->conditionOffset = ByteVectorSize(&state->control) + 1;
    state->currentBlock->condition = value;
}

boolean ParseStateBlockEnd(ParseState *state, boolean isElse)
{
    Block *block = state->currentBlock;
    intvector *locals = &block->locals;
    intvector *oldLocals = &block->parent->locals;
    intvector *unfinishedLocals;
    uint i;
    int flags;
    int oldFlags;
    int unfinishedFlags;
    uint size;
    boolean keepBlock = false;
    assert(!ParseStateBlockEmpty(state));
    state->currentBlock = block->parent;
    if (isElse)
    {
        if (!block->allowTrailingElse)
        {
            return false;
        }
        assert(!block->unfinished);
        if (!ParseStateBlockBegin(state, block->indent, false, false))
        {
            return false;
        }
        state->currentBlock->unfinished = block;
        state->currentBlock->conditionOffset =
            ByteVectorSize(&state->control) + 1;
        if (!ByteVectorAdd(&state->control, OP_JUMP) ||
            !ByteVectorAddUint(&state->control, 0))
        {
            return false;
        }

        unfinishedLocals = locals;
        locals = &state->currentBlock->locals;
        assert(IntVectorSize(locals) == IntVectorSize(oldLocals));
        for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
        {
            unfinishedFlags =
                IntVectorGet(unfinishedLocals, i + LOCAL_OFFSET_FLAGS);
            if (unfinishedFlags & LOCAL_FLAG_ACCESSED)
            {
                IntVectorSet(locals, i + LOCAL_OFFSET_VALUE,
                             IntVectorGet(unfinishedLocals,
                                          i + LOCAL_OFFSET_ACCESSOFFSET));
                IntVectorSet(
                    locals, i + LOCAL_OFFSET_FLAGS,
                    IntVectorGet(locals,
                                 i + LOCAL_OFFSET_FLAGS) | LOCAL_FLAG_ACCESSED);
                IntVectorSet(locals, i + LOCAL_OFFSET_ACCESSOFFSET,
                             IntVectorGet(unfinishedLocals,
                                          i + LOCAL_OFFSET_ACCESSOFFSET));
            }
        }
        for (i = IntVectorSize(locals); i < IntVectorSize(unfinishedLocals);
             i += LOCAL_ENTRY_SIZE)
        {
            unfinishedFlags = IntVectorGet(unfinishedLocals,
                                           i + LOCAL_OFFSET_FLAGS);
            IntVectorAdd4(
                locals,
                IntVectorGet(unfinishedLocals, i + LOCAL_OFFSET_IDENTIFIER),
                unfinishedFlags & LOCAL_FLAG_ACCESSED ?
                IntVectorGet(unfinishedLocals,
                             i + LOCAL_OFFSET_ACCESSOFFSET) : 0,
                unfinishedFlags & LOCAL_FLAG_ACCESSED,
                IntVectorGet(unfinishedLocals, i + LOCAL_OFFSET_ACCESSOFFSET));
        }
        assert(IntVectorSize(locals) == IntVectorSize(unfinishedLocals));
        keepBlock = true;
    }
    else if (block->loop)
    {
        assert(state->loopLevel > 0);
        assert(!block->unfinished);
        state->loopLevel--;
        addBranchTarget(state, block->loopBegin);
        if (!ByteVectorAdd(&state->control, OP_LOOP) ||
            !ByteVectorAddPackUint(&state->control, block->loopBegin))
        {
            return false;
        }
        for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
        {
            if (i >= IntVectorSize(oldLocals))
            {
                if (state->loopLevel > 0)
                {
                    if (ParseStateGetVariable(
                            state,
                            (stringref)IntVectorGet(
                                locals, i + LOCAL_OFFSET_IDENTIFIER)) < 0)
                    {
                        return false;
                    }
                }
                else
                {
                    IntVectorAdd4(
                        oldLocals,
                        IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                        0, 0, 0);
                }
                assert(IntVectorSize(oldLocals) == i + LOCAL_ENTRY_SIZE);
            }
            flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
            if (flags & LOCAL_FLAG_ACCESSED)
            {
                ByteVectorSetInt(
                    &state->data,
                    (uint)IntVectorGet(locals,
                                       i + LOCAL_OFFSET_ACCESSOFFSET) + 1,
                    IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE));
                ByteVectorSetInt(
                    &state->data,
                    (uint)IntVectorGet(locals,
                                       i + LOCAL_OFFSET_ACCESSOFFSET) + 5,
                    IntVectorGet(
                        flags & LOCAL_FLAG_MODIFIED ? locals : oldLocals,
                        i + LOCAL_OFFSET_VALUE));
                ByteVectorSetInt(
                    &state->data,
                    (uint)IntVectorGet(locals,
                                       i + LOCAL_OFFSET_ACCESSOFFSET) + 9,
                    (int)block->condition);
            }
            if (flags & LOCAL_FLAG_MODIFIED)
            {
                size = ByteVectorSize(&state->data);
                if (!ByteVectorAdd(&state->data, DATAOP_PHI_VARIABLE) ||
                    !ByteVectorAddInt(
                        &state->data,
                        IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE)) ||
                    !ByteVectorAddInt(
                        &state->data,
                        IntVectorGet(locals, i + LOCAL_OFFSET_VALUE)) ||
                    !ByteVectorAddInt(&state->data, (int)block->condition))
                {
                    return false;
                }
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE, (int)size);
                IntVectorSet(
                    oldLocals, i + LOCAL_OFFSET_FLAGS,
                    IntVectorGet(oldLocals,
                                 i + LOCAL_OFFSET_FLAGS) | LOCAL_FLAG_MODIFIED);
            }
        }
    }
    else if (block->unfinished)
    {
        assert(!block->unfinished->unfinished);
        unfinishedLocals = &block->unfinished->locals;
        assert(IntVectorSize(locals) >= IntVectorSize(oldLocals));
        assert(IntVectorSize(locals) >= IntVectorSize(unfinishedLocals));
        for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
        {
            flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
            unfinishedFlags = i < IntVectorSize(unfinishedLocals) ?
                IntVectorGet(unfinishedLocals, i + LOCAL_OFFSET_FLAGS) : 0;
            if (i >= IntVectorSize(oldLocals))
            {
                IntVectorAdd4(oldLocals,
                              IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                              0, 0, 0);
            }
            oldFlags = IntVectorGet(oldLocals, i + LOCAL_OFFSET_FLAGS);
            if (!(oldFlags & LOCAL_FLAG_ACCESSED))
            {
                if (flags & LOCAL_FLAG_ACCESSED)
                {
                    oldFlags |= LOCAL_FLAG_ACCESSED;
                    IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE,
                                 IntVectorGet(locals, i + LOCAL_OFFSET_VALUE));
                    IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS, oldFlags);
                    IntVectorSet(oldLocals, i + LOCAL_OFFSET_ACCESSOFFSET,
                                 IntVectorGet(locals,
                                              i + LOCAL_OFFSET_ACCESSOFFSET));
                }
                else
                {
                    assert(!(unfinishedFlags & LOCAL_FLAG_ACCESSED)); /* TODO */
                }
            }
            if ((flags | unfinishedFlags) & LOCAL_FLAG_MODIFIED)
            {
                size = ByteVectorSize(&state->data);
                if (!ByteVectorAdd(&state->data, DATAOP_PHI_VARIABLE) ||
                    !ByteVectorAddInt(
                        &state->data,
                        i < IntVectorSize(unfinishedLocals) ?
                        IntVectorGet(unfinishedLocals,
                                     i + LOCAL_OFFSET_VALUE) : 0) ||
                    !ByteVectorAddInt(
                        &state->data,
                        IntVectorGet(locals, i + LOCAL_OFFSET_VALUE)) ||
                    !ByteVectorAddInt(&state->data,
                                      (int)block->unfinished->condition))
                {
                    return false;
                }
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE, (int)size);
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS,
                             oldFlags | LOCAL_FLAG_MODIFIED);
            }
        }
        freeBlock(block->unfinished);
    }
    else
    {
        assert(IntVectorSize(locals) >= IntVectorSize(oldLocals));
        for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
        {
            flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
            if (i >= IntVectorSize(oldLocals))
            {
                IntVectorAdd4(oldLocals,
                              IntVectorGet(locals, i + LOCAL_OFFSET_IDENTIFIER),
                              0, 0, 0);
            }
            oldFlags = IntVectorGet(oldLocals, i + LOCAL_OFFSET_FLAGS);
            if (flags & LOCAL_FLAG_ACCESSED &&
                !(oldFlags & LOCAL_FLAG_ACCESSED))
            {
                oldFlags |= LOCAL_FLAG_ACCESSED;
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS, oldFlags);
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_ACCESSOFFSET,
                             IntVectorGet(locals,
                                          i + LOCAL_OFFSET_ACCESSOFFSET));
                if (!(flags & LOCAL_FLAG_MODIFIED))
                {
                    IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE,
                                 IntVectorGet(locals, i + LOCAL_OFFSET_VALUE));
                }
            }
            if (flags & LOCAL_FLAG_MODIFIED)
            {
                size = ByteVectorSize(&state->data);
                if (!ByteVectorAdd(&state->data, DATAOP_PHI_VARIABLE) ||
                    !ByteVectorAddInt(
                        &state->data,
                        IntVectorGet(oldLocals, i + LOCAL_OFFSET_VALUE)) ||
                    !ByteVectorAddInt(
                        &state->data,
                        IntVectorGet(locals, i + LOCAL_OFFSET_VALUE)) ||
                    !ByteVectorAddInt(&state->data, (int)block->condition))
                {
                    return false;
                }
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_VALUE, (int)size);
                IntVectorSet(oldLocals, i + LOCAL_OFFSET_FLAGS,
                             oldFlags | LOCAL_FLAG_MODIFIED);
            }
        }
    }

    addBranchTarget(state, ByteVectorSize(&state->control));
    ByteVectorSetUint(&state->control, block->conditionOffset,
                      ByteVectorSize(&state->control));
    if (!keepBlock)
    {
        freeBlock(block);
    }
    return true;
}

boolean ParseStateBlockEmpty(ParseState *state)
{
    ParseStateCheck(state);
    return state->currentBlock == &state->firstBlock;
}

uint ParseStateBlockIndent(ParseState *state)
{
    assert(!ParseStateBlockEmpty(state));
    return state->currentBlock->indent;
}


int ParseStateGetVariable(ParseState *state, stringref identifier)
{
    uint i;
    int flags;
    uint size;
    intvector *locals = &state->currentBlock->locals;
    ParseStateCheck(state);
    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        if ((stringref)IntVectorGet(locals, i) == identifier)
        {
            flags = IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS);
            if (state->loopLevel > 0 &&
                !(flags & (LOCAL_FLAG_ACCESSED | LOCAL_FLAG_MODIFIED)))
            {
                size = ByteVectorSize(&state->data);
                if (!ByteVectorAdd(&state->data, DATAOP_PHI_VARIABLE) ||
                    !ByteVectorAddInt(&state->data, 0) ||
                    !ByteVectorAddInt(&state->data, 0) ||
                    !ByteVectorAddInt(&state->data, 0))
                {
                    return -1;
                }
                IntVectorSet(locals, i + LOCAL_OFFSET_FLAGS,
                             LOCAL_FLAG_ACCESSED);
                IntVectorSet(locals, i + LOCAL_OFFSET_ACCESSOFFSET,
                             (int)size);
                IntVectorSet(locals, i + LOCAL_OFFSET_VALUE, (int)size);
                return (int)size;
            }
            return IntVectorGet(locals, i + LOCAL_OFFSET_VALUE);
        }
    }
    if (state->loopLevel == 0)
    {
        return 0;
    }
    size = ByteVectorSize(&state->data);
    IntVectorAdd4(locals, (int)identifier, (int)size, LOCAL_FLAG_ACCESSED,
                  (int)size);
    if (!ByteVectorAdd(&state->data, DATAOP_PHI_VARIABLE) ||
        !ByteVectorAddInt(&state->data, 0) ||
        !ByteVectorAddInt(&state->data, 0) ||
        !ByteVectorAddInt(&state->data, 0))
    {
        return -1;
    }
    return (int)size;
}

boolean ParseStateSetVariable(ParseState *state, stringref identifier,
                              int value)
{
    uint i;
    intvector *locals = &state->currentBlock->locals;
    ParseStateCheck(state);
    for (i = 0; i < IntVectorSize(locals); i += LOCAL_ENTRY_SIZE)
    {
        if ((stringref)IntVectorGet(locals, i) == identifier)
        {
            IntVectorSet(locals, i + LOCAL_OFFSET_VALUE, value);
            IntVectorSet(locals, i + LOCAL_OFFSET_FLAGS,
                         IntVectorGet(locals, i + LOCAL_OFFSET_FLAGS)
                         | LOCAL_FLAG_MODIFIED);
            return true;
        }
    }
    IntVectorAdd4(locals, (int)identifier, value, LOCAL_FLAG_MODIFIED, 0);
    return true;
}


void ParseStateSetArgument(ParseState *state, uint argumentOffset,
                           uint parameterIndex, int value)
{
    ParseStateCheck(state);
    ByteVectorSetInt(&state->control,
                     argumentOffset + parameterIndex * (uint)sizeof(int),
                     value);
}


int ParseStateWriteStringLiteral(ParseState *state, stringref value)
{
    uint size = ByteVectorSize(&state->data);
    ParseStateCheck(state);
    return ByteVectorAdd(&state->data, DATAOP_STRING) &&
        ByteVectorAddPackUint(&state->data, (uint)value) ? (int)size : -1;
}


boolean ParseStateWriteIf(ParseState *state, int value)
{
    ParseStateCheck(state);
    ParseStateBlockSetCondition(state, value);
    return ByteVectorAdd(&state->control, OP_BRANCH) &&
        ByteVectorAddInt(&state->control, 0) &&
        ByteVectorAddPackInt(&state->control, value) ? true : false;
}

boolean ParseStateWriteWhile(ParseState *state, int value)
{
    ParseStateCheck(state);
    ParseStateBlockSetCondition(state, value);
    return ByteVectorAdd(&state->control, OP_BRANCH) &&
        ByteVectorAddInt(&state->control, 0) &&
        ByteVectorAddPackInt(&state->control, value) ? true : false;
}

boolean ParseStateWriteReturn(ParseState *state)
{
    ParseStateCheck(state);
    return ByteVectorAdd(&state->control, OP_RETURN);
}

uint ParseStateWriteNativeInvocation(ParseState *state,
                                     nativefunctionref nativeFunction,
                                     uint parameterCount)
{
    uint argumentOffset;
    ParseStateCheck(state);
    if (!ByteVectorAdd(&state->control, OP_INVOKE_NATIVE) ||
        !ByteVectorAdd(&state->control, (byte)nativeFunction) ||
        !ByteVectorAddPackUint(&state->control, parameterCount))
    {
        return 0;
    }
    argumentOffset = ByteVectorSize(&state->control);
    ByteVectorSetSize(&state->control,
                      argumentOffset + parameterCount * (uint)sizeof(int));
    ByteVectorFill(&state->control, argumentOffset,
                   parameterCount * (uint)sizeof(int), 0);
    return argumentOffset;
}
