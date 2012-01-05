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
    stringref filename;
    uint line;
    uint fileOffset;
    uint bytecodeStart;
    uint bytecodeStop;
} FieldInfo;

static bytevector fieldTable;
static intvector fieldValues;


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
    IVInit(&fieldValues, 128);
    IVSetSize(&fieldValues, RESERVED_FIELD_COUNT);
    IVSet(&fieldValues, FIELD_NULL, 0);
    IVSet(&fieldValues, FIELD_TRUE, HeapTrue);
    IVSet(&fieldValues, FIELD_FALSE, HeapFalse);
    IVSet(&fieldValues, FIELD_EMPTY_LIST, HeapEmptyList);
}

void FieldIndexDispose(void)
{
    BVDispose(&fieldTable);
    IVDispose(&fieldValues);
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


static fieldref addField(namespaceref ns, stringref filename, uint line,
                         uint fileOffset, objectref value)
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
    IVAdd(&fieldValues, value);
    return refFromSize(IVSize(&fieldValues));
}

fieldref FieldIndexAdd(namespaceref ns,
                       stringref filename, uint line, uint fileOffset)
{
    return addField(ns, filename, line, fileOffset, 0);
}

fieldref FieldIndexAddConstant(namespaceref ns,
                               stringref filename, uint line, uint fileOffset,
                               bytevector *bytecode, size_t start)
{
    fieldref field = FieldIndexAdd(ns, filename, line, fileOffset);
    FieldIndexSetBytecodeOffset(field, start, BVSize(bytecode));
    return field;
}

fieldref FieldIndexAddIntegerConstant(int value)
{
    return addField(0, 0, 0, 0, HeapBoxInteger(value));
}

fieldref FieldIndexAddStringConstant(stringref string)
{
    return addField(0, 0, 0, 0, HeapCreatePooledString(string));
}

fieldref FieldIndexAddFileConstant(stringref string)
{
    return addField(0, 0, 0, 0, HeapCreatePath(HeapCreatePooledString(string)));
}

fieldref FieldIndexAddListConstant(const intvector *values)
{
    byte *objectData;
    size_t size = IVSize(values);

    objectData = HeapAlloc(TYPE_ARRAY, size * sizeof(objectref));
    objectData += size * sizeof(objectref);
    while (size--)
    {
        objectData -= sizeof(objectref);
        *(objectref*)objectData =
            IVGet(&fieldValues, FieldIndexGetIndex(IVGet(values, size)));
    }
    return addField(0, 0, 0, 0, HeapFinishAlloc(objectData));
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
    return IVSize(&fieldValues);
}

boolean FieldIndexIsConstant(fieldref field)
{
    return field <= RESERVED_FIELD_COUNT || !FieldIndexGetFilename(field);
}

objectref FieldIndexValue(fieldref field)
{
    return IVGet(&fieldValues, uintFromRef(field) - 1);
}

void FieldIndexCopyValues(objectref *target)
{
    memcpy(target, IVGetPointer(&fieldValues, 0),
           IVSize(&fieldValues) * sizeof(objectref));
}

fieldref FieldIndexGetFirstField(void)
{
    return refFromUint(IVSize(&fieldValues) > RESERVED_FIELD_COUNT ?
                       RESERVED_FIELD_COUNT + 1 : 0);
}

fieldref FieldIndexGetNextField(fieldref field)
{
    assert(sizeFromRef(field) > RESERVED_FIELD_COUNT);
    assert(sizeFromRef(field) <= IVSize(&fieldValues));
    return sizeFromRef(field) != IVSize(&fieldValues) ? field + 1 : 0;
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
