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
#define TABLE_ENTRY_BYTECODE_OFFSET 4
#define TABLE_ENTRY_PARAMETER_COUNT 5
#define TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT 6
#define TABLE_ENTRY_SIZE 7

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

static boolean isTarget(targetref target)
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

void TargetIndexInit(void)
{
    IntVectorInit(&targetInfo);
    /* Position 0 is reserved to mean invalid target. */
    IntVectorAdd(&targetInfo, 0);

    IntVectorInit(&parseQueue);

    targetCount = 0;
    hasIndex = false;
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
    assert(isTarget(target));
    target = (targetref)((uint)target + getTargetSize(target));
    return (uint)target < IntVectorSize(&targetInfo) ? target : 0;
}

boolean TargetIndexBeginTarget(stringref name, fileref file, uint line,
                               uint fileOffset)
{
    assert(!hasIndex);
    currentTarget = (targetref)IntVectorSize(&targetInfo);
    IntVectorAdd(&targetInfo, (uint)name);
    IntVectorAdd(&targetInfo, (uint)file);
    IntVectorAdd(&targetInfo, line);
    IntVectorAdd(&targetInfo, fileOffset);
    IntVectorAdd(&targetInfo, 0);
    IntVectorAdd(&targetInfo, 0);
    IntVectorAdd(&targetInfo, 0);
    targetCount++;
    return true;
}

void TargetIndexFinishTarget(void)
{
}

void TargetIndexMarkForParsing(targetref target)
{
    if (!TargetIndexGetBytecodeOffset(target))
    {
        IntVectorAdd(&parseQueue, target);
    }
}

targetref TargetIndexPopUnparsedTarget(void)
{
    return IntVectorSize(&parseQueue) ? IntVectorPop(&parseQueue) : 0;
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

stringref TargetIndexGetName(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return (stringref)IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_NAME);
}

fileref TargetIndexGetFile(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FILE);
}

uint TargetIndexGetLine(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_LINE);
}

uint TargetIndexGetFileOffset(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_FILE_OFFSET);
}

uint TargetIndexGetBytecodeOffset(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_BYTECODE_OFFSET);
}

void TargetIndexSetBytecodeOffset(targetref target, uint offset)
{
    assert(hasIndex);
    assert(isTarget(target));
    IntVectorSet(&targetInfo, (uint)target + TABLE_ENTRY_BYTECODE_OFFSET,
                 offset);
}

uint TargetIndexGetParameterCount(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_PARAMETER_COUNT);
}

const stringref *TargetIndexGetParameterNames(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return (stringref*)IntVectorGetPointer(&targetInfo, (uint)target + TABLE_ENTRY_SIZE);
}

uint TargetIndexGetMinimumArgumentCount(targetref target)
{
    assert(hasIndex);
    assert(isTarget(target));
    return IntVectorGet(&targetInfo, (uint)target + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT);
}
