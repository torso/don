#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "instruction.h"

#define CONSTANT_COUNT 4

typedef struct
{
    fileref file;
    uint line;
    uint fileOffset;
    uint bytecodeStart;
    uint bytecodeStop;
} FieldInfo;

static bytevector fieldTable;
static uint fieldCount;


static FieldInfo *getFieldInfo(fieldref field)
{
    assert(field);
    return (FieldInfo*)ByteVectorGetPointer(
        &fieldTable, (field - CONSTANT_COUNT - 1) * sizeof(FieldInfo));
}


void FieldIndexInit(void)
{
    ByteVectorInit(&fieldTable, 1024);
}

void FieldIndexDispose(void)
{
    ByteVectorDispose(&fieldTable);
}


static void writeConstant(bytevector *bytecode, Instruction op, uint index)
{
    ByteVectorAdd(bytecode, op);
    ByteVectorAdd(bytecode, OP_STORE_FIELD);
    ByteVectorAddUint(bytecode, index);
}

void FieldIndexFinishBytecode(const byte *parsed, bytevector *bytecode)
{
    fieldref field;
    FieldInfo *info;

    writeConstant(bytecode, OP_NULL, 0);
    writeConstant(bytecode, OP_TRUE, 1);
    writeConstant(bytecode, OP_FALSE, 2);
    writeConstant(bytecode, OP_EMPTY_LIST, 3);

    for (field = FieldIndexGetFirstField();
         field;
         field = FieldIndexGetNextField(field))
    {
        info = getFieldInfo(field);
        if (info->bytecodeStop)
        {
            ByteVectorAddData(bytecode, &parsed[info->bytecodeStart],
                              info->bytecodeStop - info->bytecodeStart);
            ByteVectorAdd(bytecode, OP_STORE_FIELD);
            ByteVectorAddUint(bytecode, FieldIndexGetIndex(field));
        }
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
    return refFromUint(fieldCount + CONSTANT_COUNT);
}

fieldref FieldIndexAddConstant(fileref file, uint line, uint fileOffset,
                               bytevector *bytecode, size_t start)
{
    fieldref field;
    size_t size = ByteVectorSize(bytecode);

    if (size - start == 1)
    {
        switch (ByteVectorGet(bytecode, start))
        {
        case OP_NULL:
            return 1;
        case OP_TRUE:
            return 2;
        case OP_FALSE:
            return 3;
        case OP_EMPTY_LIST:
            return 4;
        }
    }
    field = FieldIndexAdd(file, line, fileOffset);
    FieldIndexSetBytecodeOffset(field, start, ByteVectorSize(bytecode));
    return field;
}

void FieldIndexSetBytecodeOffset(fieldref field, size_t start, size_t stop)
{
    FieldInfo *info = getFieldInfo(field);
    assert(start <= UINT_MAX - 1);
    assert(stop <= UINT_MAX - 1);
    assert(stop > start);
    info->bytecodeStart = (uint)start;
    info->bytecodeStop = (uint)stop;
}


uint FieldIndexGetCount(void)
{
    return fieldCount + CONSTANT_COUNT;
}

fieldref FieldIndexGetFirstField(void)
{
    return refFromUint(fieldCount ? CONSTANT_COUNT + 1 : 0);
}

fieldref FieldIndexGetNextField(fieldref field)
{
    assert(field > CONSTANT_COUNT);
    assert(field - CONSTANT_COUNT <= fieldCount);
    return field - CONSTANT_COUNT != fieldCount ? field + 1 : 0;
}

uint FieldIndexGetIndex(fieldref field)
{
    return uintFromRef(field) - 1;
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
