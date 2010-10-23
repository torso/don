#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "instruction.h"

typedef struct
{
    fileref file;
    uint line;
    uint fileOffset;
    uint bytecodeStart;
    uint bytecodeStop;
} FieldInfo;

bytevector fieldTable;
uint fieldCount;


static FieldInfo *getFieldInfo(fieldref field)
{
    return (FieldInfo*)ByteVectorGetPointer(&fieldTable, sizeFromRef(field));
}


void FieldIndexInit(void)
{
    ByteVectorInit(&fieldTable, 1024);
    /* Position 0 is reserved to mean invalid. */
    ByteVectorSetSize(&fieldTable, sizeof(int));
}

void FieldIndexDispose(void)
{
    ByteVectorDispose(&fieldTable);
}


void FieldIndexFinishBytecode(const byte *parsed, bytevector *bytecode)
{
    fieldref field;
    FieldInfo *info;

    for (field = FieldIndexGetFirstField();
         field;
         field = FieldIndexGetNextField(field))
    {
        info = getFieldInfo(field);
        if (info->bytecodeStop)
        {
            ByteVectorAddData(bytecode, &parsed[info->bytecodeStart],
                              info->bytecodeStop - info->bytecodeStart);
        }
        else
        {
            ByteVectorAdd(bytecode, OP_UNKNOWN_VALUE);
        }
        ByteVectorAdd(bytecode, OP_STORE_FIELD);
        ByteVectorAddUint(bytecode, FieldIndexGetIndex(field));
    }
    ByteVectorAdd(bytecode, OP_RETURN_VOID);
}


fieldref FieldIndexAdd(fileref file, uint line, uint fileOffset)
{
    size_t size = ByteVectorSize(&fieldTable);
    FieldInfo *info;

    ByteVectorSetSize(&fieldTable, size + sizeof(FieldInfo));
    fieldCount++;
    info = (FieldInfo*)ByteVectorGetPointer(&fieldTable, size);
    info->file = file;
    info->line = line;
    info->fileOffset = fileOffset;
    info->bytecodeStop = 0;
    return (fieldref)size;
}

void FieldIndexSetBytecodeOffset(fieldref field, size_t start, size_t stop)
{
    FieldInfo *info = getFieldInfo(field);
    assert(start <= UINT_MAX);
    assert(stop <= UINT_MAX);
    assert(stop > start);
    info->bytecodeStart = (uint)start;
    info->bytecodeStop = (uint)stop;
}


uint FieldIndexGetCount(void)
{
    return fieldCount;
}

fieldref FieldIndexGetFirstField(void)
{
    return refFromUint(fieldCount ? sizeof(int) : 0);
}

fieldref FieldIndexGetNextField(fieldref field)
{
    field = refFromSize(sizeFromRef(field) + sizeof(FieldInfo));
    if (field == refFromSize(ByteVectorSize(&fieldTable)))
    {
        return 0;
    }
    return field;
}

uint FieldIndexGetIndex(fieldref field)
{
    return (uint)(sizeFromRef(field) - sizeof(int)) / (uint)sizeof(FieldInfo);
}

fileref FieldIndexGetFile(fieldref field)
{
    return getFieldInfo(field)->file;
}

uint FieldIndexGetLine(fieldref field)
{
    return getFieldInfo(field)->line;
}

uint FieldIndexGetFileOffset(fieldref field)
{
    return getFieldInfo(field)->fileOffset;
}
