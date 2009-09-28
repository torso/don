#include <stdlib.h>
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

static intvector table;

void TargetIndexInit()
{
    IntVectorInit(&table);
}

void TargetIndexFree()
{
    IntVectorFree(&table);
}

targetref TargetIndexAdd(stringref name, fileref file, int line, int offset)
{
    uint ref = IntVectorSize(&table);
    IntVectorAdd4(&table, name, file, line, offset);
    return ref;
}

stringref TargetIndexGetName(targetref target)
{
    return IntVectorGet(&table, target * TABLE_ENTRY_SIZE + TABLE_ENTRY_NAME);
}

fileref TargetIndexGetFile(targetref target)
{
    return IntVectorGet(&table, target * TABLE_ENTRY_SIZE + TABLE_ENTRY_FILE);
}

uint TargetIndexGetLine(targetref target)
{
    return IntVectorGet(&table, target * TABLE_ENTRY_SIZE + TABLE_ENTRY_LINE);
}

uint TargetIndexGetOffset(targetref target)
{
    return IntVectorGet(&table, target * TABLE_ENTRY_SIZE + TABLE_ENTRY_OFFSET);
}
