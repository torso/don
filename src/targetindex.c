#include <stdlib.h>
#include <memory.h>
#include "builder.h"
#include "intvector.h"
#include "inthashmap.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"

#define TABLE_ENTRY_NAME 0
#define TABLE_ENTRY_FILE 1
#define TABLE_ENTRY_LINE 2
#define TABLE_ENTRY_FILE_OFFSET 3
#define TABLE_ENTRY_FLAGS 4
#define TABLE_ENTRY_BYTECODE_OFFSET 5
#define TABLE_ENTRY_PARAMETER_COUNT 6
#define TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT 7
#define TABLE_ENTRY_SIZE 8

#define TARGET_FLAG_TARGET 1
#define TARGET_FLAG_QUEUED 2

static intvector targetInfo;
static intvector parseQueue;
static inthashmap targetIndex;
static uint targetCount;
static boolean hasIndex;

/*
  This value is used temporarily between TargetIndexBeginTarget and
  TargetIndexFinishTarget.
 */
static targetref currentTarget;


static uint getTargetSize(targetref target)
{
    return TABLE_ENTRY_SIZE +
        IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_PARAMETER_COUNT);
}

static boolean isValidTarget(targetref target)
{
    uint offset;
    if (!target || !targetCount || (uint)target >= IntVectorSize(&targetInfo))
    {
        return false;
    }
    for (offset = 1;
         offset < IntVectorSize(&targetInfo);
         offset += getTargetSize((targetref)offset))
    {
        if (offset >= (uint)target)
        {
            return offset == (uint)target ? true : false;
        }
    }
    return false;
}

static uint getFlag(targetref target, uint flag)
{
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FLAGS) & flag;
}

static uint getAndResetFlag(targetref target, uint flag)
{
    uint flags = IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FLAGS);
    IntVectorSet(&targetInfo, (uint)target + TABLE_ENTRY_FLAGS, flags & ~flag);
    return flags & flag;
}

static void setFlag(targetref target, uint flag)
{
    IntVectorSet(
        &targetInfo,
        (uint)target + TABLE_ENTRY_FLAGS,
        IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FLAGS) | flag);
}

ErrorCode TargetIndexInit(void)
{
    ErrorCode error;

    IntVectorInit(&targetInfo);
    /* Position 0 is reserved to mean invalid target. */
    error = IntVectorAdd(&targetInfo, 0);
    if (error)
    {
        return error;
    }

    IntVectorInit(&parseQueue);

    targetCount = 0;
    hasIndex = false;
    return NO_ERROR;
}

void TargetIndexDispose(void)
{
    IntVectorDispose(&targetInfo);
    IntVectorDispose(&parseQueue);
    if (hasIndex)
    {
        IntHashMapDispose(&targetIndex);
    }
}

boolean TargetIndexBuildIndex(void)
{
    /* uint tableSize; */
    targetref target;

    assert(!hasIndex);
    if (!IntHashMapInit(&targetIndex, TargetIndexGetTargetCount()))
    {
        return false;
    }
    hasIndex = true;

    for (target = TargetIndexGetFirstTarget();
         target;
         target = TargetIndexGetNextTarget(target))
    {
        IntHashMapAdd(&targetIndex, (uint)TargetIndexGetName(target), (uint)target);
    }
    return true;
}

targetref TargetIndexGetFirstTarget(void)
{
    return targetCount ? 1 : 0;
}

targetref TargetIndexGetNextTarget(targetref target)
{
    assert(isValidTarget(target));
    target = (targetref)((uint)target + getTargetSize(target));
    return (uint)target < IntVectorSize(&targetInfo) ? target : 0;
}

ErrorCode TargetIndexBeginTarget(stringref name)
{
    ErrorCode error;

    assert(!hasIndex);
    currentTarget = (targetref)IntVectorSize(&targetInfo);
    error = IntVectorAdd(&targetInfo, (uint)name);
    if (error)
    {
        return error;
    }
    error = IntVectorGrowZero(&targetInfo, TABLE_ENTRY_SIZE - 1);
    if (error)
    {
        return error;
    }
    targetCount++;
    return NO_ERROR;
}

ErrorCode TargetIndexAddParameter(stringref name, boolean required)
{
    uint parameterCount =
        IntVectorGet(&targetInfo,
                     (uint)currentTarget + TABLE_ENTRY_PARAMETER_COUNT);
    uint minArgumentCount =
        IntVectorGet(&targetInfo,
                     (uint)currentTarget + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT);

    assert(!hasIndex);
    assert(!required || parameterCount == minArgumentCount);
    IntVectorSet(&targetInfo, (uint)currentTarget + TABLE_ENTRY_PARAMETER_COUNT,
                 parameterCount + 1);
    if (required)
    {
        IntVectorSet(&targetInfo,
                     (uint)currentTarget + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT,
                     minArgumentCount + 1);
    }
    return IntVectorAdd(&targetInfo, (uint)name);
}

void TargetIndexFinishTarget(fileref file, uint line, uint fileOffset,
                             boolean isTarget)
{
    assert(!isTarget ||
           !IntVectorGet(&targetInfo,
                         (uint)currentTarget + TABLE_ENTRY_PARAMETER_COUNT));
    IntVectorSet(&targetInfo, (uint)currentTarget + TABLE_ENTRY_FILE, file);
    IntVectorSet(&targetInfo, (uint)currentTarget + TABLE_ENTRY_LINE, line);
    IntVectorSet(&targetInfo, (uint)currentTarget + TABLE_ENTRY_FILE_OFFSET,
                 fileOffset);
    if (isTarget)
    {
        setFlag(currentTarget, TARGET_FLAG_TARGET);
    }
}

void TargetIndexMarkForParsing(targetref target)
{
    if (!TargetIndexGetBytecodeOffset(target) &&
        !getFlag(target, TARGET_FLAG_QUEUED))
    {
        setFlag(target, TARGET_FLAG_QUEUED);
        IntVectorAdd(&parseQueue, target);
    }
}

targetref TargetIndexPopUnparsedTarget(void)
{
    targetref target;
    while (IntVectorSize(&parseQueue))
    {
        target = IntVectorPop(&parseQueue);
        if (getAndResetFlag(target, TARGET_FLAG_QUEUED))
        {
            return target;
        }
    }
    return 0;
}

uint TargetIndexGetTargetCount(void)
{
    return targetCount;
}

targetref TargetIndexGet(stringref name)
{
    assert(hasIndex);
    return (targetref)IntHashMapGet(&targetIndex, (uint)name);
}

targetref TargetIndexGetTargetFromBytecode(uint bytecodeOffset)
{
    targetref target;
    for (target = TargetIndexGetFirstTarget();
         target;
         target = TargetIndexGetNextTarget(target))
    {
        if (TargetIndexGetBytecodeOffset(target) == bytecodeOffset)
        {
            return target;
        }
    }
    return 0;
}

stringref TargetIndexGetName(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return (stringref)IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_NAME);
}

fileref TargetIndexGetFile(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FILE);
}

uint TargetIndexGetLine(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_LINE);
}

uint TargetIndexGetFileOffset(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FILE_OFFSET);
}

boolean TargetIndexIsTarget(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return getFlag(target, TARGET_FLAG_TARGET) ? true : false;
}

uint TargetIndexGetBytecodeOffset(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_BYTECODE_OFFSET);
}

void TargetIndexSetBytecodeOffset(targetref target, uint offset)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    IntVectorSet(&targetInfo, (uint)target + TABLE_ENTRY_BYTECODE_OFFSET,
                 offset);
}

uint TargetIndexGetParameterCount(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_PARAMETER_COUNT);
}

const stringref *TargetIndexGetParameterNames(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return (stringref*)IntVectorGetPointer(&targetInfo, (uint)target + TABLE_ENTRY_SIZE);
}

uint TargetIndexGetMinimumArgumentCount(targetref target)
{
    assert(hasIndex);
    assert(isValidTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT);
}
