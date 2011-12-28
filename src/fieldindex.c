#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "instruction.h"

typedef struct
{
    namespaceref ns;
    stringref filename;
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
    return (FieldInfo*)BVGetPointer(
        &fieldTable,
        (sizeFromRef(field) - RESERVED_FIELD_COUNT - 1) * sizeof(FieldInfo));
}


void FieldIndexInit(void)
{
    BVInit(&fieldTable, 1024);
}

void FieldIndexDispose(void)
{
    BVDispose(&fieldTable);
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
            BVAddData(bytecode, &parsed[info->bytecodeStart],
                      info->bytecodeStop - info->bytecodeStart);
            BVAdd(bytecode, OP_STORE_FIELD);
            BVAddUint(bytecode, FieldIndexGetIndex(field));
        }
    }
    BVAdd(bytecode, OP_RETURN_VOID);
}


fieldref FieldIndexAdd(namespaceref ns,
                       stringref filename, uint line, uint fileOffset)
{
    size_t size = BVSize(&fieldTable);
    FieldInfo *info;

    BVSetSize(&fieldTable, size + sizeof(FieldInfo));
    fieldCount++;
    info = (FieldInfo*)BVGetPointer(&fieldTable, size);
    info->ns = ns;
    info->filename = filename;
    info->line = line;
    info->fileOffset = fileOffset;
    info->bytecodeStop = 0;
    return refFromUint(fieldCount + RESERVED_FIELD_COUNT);
}

fieldref FieldIndexAddConstant(namespaceref ns,
                               stringref filename, uint line, uint fileOffset,
                               bytevector *bytecode, size_t start)
{
    fieldref field = FieldIndexAdd(ns, filename, line, fileOffset);
    FieldIndexSetBytecodeOffset(field, start, BVSize(bytecode));
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
    return fieldCount + RESERVED_FIELD_COUNT;
}

fieldref FieldIndexGetFirstField(void)
{
    return refFromUint(fieldCount ? RESERVED_FIELD_COUNT + 1 : 0);
}

fieldref FieldIndexGetNextField(fieldref field)
{
    assert(sizeFromRef(field) > RESERVED_FIELD_COUNT);
    assert(sizeFromRef(field) - RESERVED_FIELD_COUNT <= fieldCount);
    return sizeFromRef(field) - RESERVED_FIELD_COUNT != fieldCount ?
        field + 1 : 0;
}

uint FieldIndexGetIndex(fieldref field)
{
    return uintFromRef(field) - 1;
}

namespaceref FieldIndexGetNamespace(fieldref field)
{
    return getFieldInfo(field)->ns;
}

stringref FieldIndexGetFilename(fieldref field)
{
    return getFieldInfo(field)->filename;
}

uint FieldIndexGetLine(fieldref field)
{
    return getFieldInfo(field)->line;
}

uint FieldIndexGetFileOffset(fieldref field)
{
    return getFieldInfo(field)->fileOffset;
}
