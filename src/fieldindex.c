#include <memory.h>
#include "common.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "instruction.h"
#include "heap.h"
#include "intvector.h"

typedef struct
{
    namespaceref ns;
    objectref filename;
    uint line;
    uint fileOffset;
    uint bytecodeStart;
    uint bytecodeStop;
} FieldInfo;

static bytevector fieldTable;
static size_t fieldCount;


static FieldInfo *getFieldInfo(fieldref field)
{
    assert(field);
    return (FieldInfo*)BVGetPointer(
        &fieldTable,
        (sizeFromRef(field) - 1) * sizeof(FieldInfo));
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


static fieldref addField(namespaceref ns, objectref filename, uint line,
                         uint fileOffset)
{
    size_t size = BVSize(&fieldTable);
    FieldInfo *info;

    BVSetSize(&fieldTable, size + sizeof(FieldInfo));
    info = (FieldInfo*)BVGetPointer(&fieldTable, size);
    info->ns = ns;
    info->filename = filename;
    info->line = line;
    info->fileOffset = fileOffset;
    info->bytecodeStop = 0;
    return refFromSize(++fieldCount);
}

fieldref FieldIndexAdd(namespaceref ns,
                       objectref filename, uint line, uint fileOffset)
{
    return addField(ns, filename, line, fileOffset);
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


size_t FieldIndexGetCount(void)
{
    return fieldCount;
}

fieldref FieldIndexGetFirstField(void)
{
    return min(refFromSize(fieldCount), 1);
}

fieldref FieldIndexGetNextField(fieldref field)
{
    assert(sizeFromRef(field) <= fieldCount);
    return sizeFromRef(field) != fieldCount ? field + 1 : 0;
}

uint FieldIndexGetIndex(fieldref field)
{
    return uintFromRef(field) - 1;
}

fieldref FieldIndexFromIndex(uint index)
{
    return refFromUint(index + 1);
}

namespaceref FieldIndexGetNamespace(fieldref field)
{
    return getFieldInfo(field)->ns;
}

objectref FieldIndexGetFilename(fieldref field)
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
