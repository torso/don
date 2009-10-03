#include <stdlib.h>
#include <memory.h>
#include "builder.h"
#include "intvector.h"
#include "stringpool.h"
#include "fileindex.h"
#include "targetindex.h"

#define TABLE_ENTRY_NAME 0
#define TABLE_ENTRY_FILE 1
#define TABLE_ENTRY_LINE 2
#define TABLE_ENTRY_OFFSET 3
#define TABLE_ENTRY_SIZE 4
#define TABLE_ENTRY_BYTE_SIZE (TABLE_ENTRY_SIZE * sizeof(uint))

static intvector unsortedTable;
static uint* table;
static uint targetCount;

static void insertionSort(uint* restrict target, const uint* restrict source,
                          uint size)
{
    uint i;
    uint j;
    for (i = 0; i < size; i++)
    {
        for (j = i;
             j > 0 &&
                 target[(j - 1) * TABLE_ENTRY_SIZE] >
                 source[i * TABLE_ENTRY_SIZE];
             j--);
        memmove(&target[(j + 1) * TABLE_ENTRY_SIZE],
                &target[j * TABLE_ENTRY_SIZE],
                (i - j) * TABLE_ENTRY_BYTE_SIZE);
        memcpy(&target[j * TABLE_ENTRY_SIZE],
               &source[i * TABLE_ENTRY_SIZE],
               TABLE_ENTRY_BYTE_SIZE);
    }
}

static int copySmallerEntries(uint* restrict target,
                              const uint* restrict source,
                              uint value,
                              uint maxEntries)
{
    uint copyCount;
    for (copyCount = 0;
         copyCount < maxEntries &&
             source[copyCount * TABLE_ENTRY_SIZE + TABLE_ENTRY_NAME] <= value;
         copyCount++);
    memcpy(target, source, copyCount * TABLE_ENTRY_BYTE_SIZE);
    return copyCount;
}

static void mergeSort(uint* restrict target, const uint* restrict source,
                      uint size1, uint size2)
{
    uint copied;
    const uint* restrict source2 = source + size1 * TABLE_ENTRY_SIZE;
    assert(size1 > 0);
    assert(size2 > 0);
    for (;;)
    {
        copied = copySmallerEntries(target, source, source2[TABLE_ENTRY_NAME],
                                    size1);
        assert(copied <= size1);
        source += copied * TABLE_ENTRY_SIZE;
        target += copied * TABLE_ENTRY_SIZE;
        size1 -= copied;
        if (size1 == 0)
        {
            memcpy(target, source2, size2 * TABLE_ENTRY_BYTE_SIZE);
            break;
        }

        copied = copySmallerEntries(target, source2, source[TABLE_ENTRY_NAME],
                                    size2);
        assert(copied <= size2);
        source2 += copied * TABLE_ENTRY_SIZE;
        target += copied * TABLE_ENTRY_SIZE;
        size2 -= copied;
        if (size2 == 0)
        {
            memcpy(target, source, size1 * TABLE_ENTRY_BYTE_SIZE);
            break;
        }
    }
}

static void sortIndex()
{
    uint* scratch;
    uint* data1;
    uint* data2;
    uint i;
    uint j;
    boolean odd;
    scratch = malloc(targetCount * TABLE_ENTRY_SIZE * sizeof(uint));
    assert(scratch); /* TODO: handle oom */
    if ((targetCount / 8) & 1)
    {
        data1 = table;
        data2 = scratch;
    }
    else
    {
        data1 = scratch;
        data2 = table;
    }
    for (i = 0; i < targetCount;)
    {
        insertionSort(&data1[i * TABLE_ENTRY_SIZE],
                      (const uint*)IntVectorGetPointer(&unsortedTable, i * TABLE_ENTRY_SIZE),
                      min(targetCount - i, 8));
        i += 8;
        if (i <= targetCount)
        {
            for (j = 8, odd = true; i - j > 0 && (i & ((j << 1) - 1)) == 0; j <<= 1, odd = !odd)
            {
                if (odd)
                {
                    mergeSort(&data2[(i - 2 * j) * TABLE_ENTRY_SIZE], &data1[(i - 2 * j) * TABLE_ENTRY_SIZE], j, j);
                }
                else
                {
                    mergeSort(&data1[(i - 2 * j) * TABLE_ENTRY_SIZE], &data2[(i - 2 * j) * TABLE_ENTRY_SIZE], j, j);
                }
            }
        }
    }
    for (i = 8, odd = true; i < targetCount; i <<= 1, odd = !odd)
    {
        if ((targetCount & i) != 0)
        {
            if (odd)
            {
                mergeSort(&data2[((targetCount - i) & ~(i - 1)) * TABLE_ENTRY_SIZE], &data1[((targetCount - i) & ~(i - 1)) * TABLE_ENTRY_SIZE], i, min(i, targetCount - ((targetCount - i) & ~(i - 1)) - i));
            }
            else
            {
                mergeSort(&data1[((targetCount - i) & ~(i - 1)) * TABLE_ENTRY_SIZE], &data2[((targetCount - i) & ~(i - 1)) * TABLE_ENTRY_SIZE], i, min(i, targetCount - ((targetCount - i) & ~(i - 1)) - i));
            }
        }
    }
    free(scratch);
}

void TargetIndexInit()
{
    IntVectorInit(&unsortedTable);
}

void TargetIndexFree()
{
    free(table);
}

targetref TargetIndexAdd(stringref name, fileref file, int line, int offset)
{
    uint ref = IntVectorSize(&unsortedTable);
    IntVectorAdd4(&unsortedTable, name, file, line, offset);
    return ref;
}

targetref TargetIndexGet(stringref name)
{
    uint low = 0;
    uint high = targetCount;
    uint mid;
    assert(table);
    assert(name);
    while (low < high)
    {
        mid = low + (high - low) / 2;
        if (table[mid * TABLE_ENTRY_SIZE + TABLE_ENTRY_NAME] < name)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    assert(low == high);
    if (low < targetCount &&
        table[low * TABLE_ENTRY_SIZE + TABLE_ENTRY_NAME] == name)
    {
        return low;
    }
    return -1;
}

stringref TargetIndexGetName(targetref target)
{
    assert(table);
    assert(target < targetCount);
    return table[target * TABLE_ENTRY_SIZE + TABLE_ENTRY_NAME];
}

fileref TargetIndexGetFile(targetref target)
{
    assert(table);
    assert(target < targetCount);
    return table[target * TABLE_ENTRY_SIZE + TABLE_ENTRY_FILE];
}

uint TargetIndexGetLine(targetref target)
{
    assert(table);
    assert(target < targetCount);
    return table[target * TABLE_ENTRY_SIZE + TABLE_ENTRY_LINE];
}

uint TargetIndexGetOffset(targetref target)
{
    assert(table);
    assert(target < targetCount);
    return table[target * TABLE_ENTRY_SIZE + TABLE_ENTRY_OFFSET];
}

void TargetIndexFinish()
{
    assert(!table);
    targetCount = IntVectorSize(&unsortedTable) / TABLE_ENTRY_SIZE;
    table = malloc(targetCount * TABLE_ENTRY_SIZE * sizeof(uint));
    assert(table); /* TODO: handle oom */
    sortIndex();
    IntVectorFree(&unsortedTable);
}
