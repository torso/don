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


ErrorCode FieldIndexInit(void)
{
    ErrorCode error = ByteVectorInit(&fieldTable);
    if (error)
    {
        return error;
    }
    /* Position 0 is reserved to mean invalid function. */
    return ByteVectorSetSize(&fieldTable, sizeof(int));
}

void FieldIndexDispose(void)
{
    ByteVectorDispose(&fieldTable);
}


ErrorCode FieldIndexFinishBytecode(const byte *parsed, bytevector *bytecode)
{
    fieldref field;
    FieldInfo *info;
    ErrorCode error;

    for (field = FieldIndexGetFirstField();
         field;
         field = FieldIndexGetNextField(field))
    {
        info = getFieldInfo(field);
        error = ByteVectorAddData(bytecode, &parsed[info->bytecodeStart],
                                  info->bytecodeStop - info->bytecodeStart);
        if (error)
        {
            return error;
        }
        error = ByteVectorAdd(bytecode, OP_STORE_FIELD);
        if (error)
        {
            return error;
        }
        error = ByteVectorAddUint(bytecode, FieldIndexGetIndex(field));
        if (error)
        {
            return error;
        }
    }
    return ByteVectorAdd(bytecode, OP_RETURN_VOID);
}


fieldref FieldIndexAdd(fileref file, uint line, uint fileOffset)
{
    size_t size = ByteVectorSize(&fieldTable);
    ErrorCode error = ByteVectorSetSize(&fieldTable, size + sizeof(FieldInfo));
    FieldInfo *info;

    if (error)
    {
        return 0;
    }
    fieldCount++;
    info = (FieldInfo*)ByteVectorGetPointer(&fieldTable, size);
    info->file = file;
    info->line = line;
    info->fileOffset = fileOffset;
    return (fieldref)size;
}

void FieldIndexSetBytecodeOffset(fieldref field, uint start, uint stop)
{
    FieldInfo *info = getFieldInfo(field);
    assert(stop > start);
    info->bytecodeStart = start;
    info->bytecodeStop = stop;
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
