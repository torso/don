#include <memory.h>
#include "common.h"
#include "bytevector.h"

static void checkByteVector(const bytevector *v)
{
    assert(v);
    assert(v->data);
    assert(v->allocatedSize);
    assert(v->allocatedSize >= v->size);
}

static void checkByteVectorIndex(const bytevector *v, size_t index)
{
    checkByteVector(v);
    assert(index < v->size);
}

static void checkByteVectorRange(const bytevector *v, size_t index, size_t size)
{
    checkByteVector(v);
    assert(index < v->size || (index == v->size && !size));
    assert(ByteVectorSize(v) >= index + size);
}

static byte *grow(bytevector *v, size_t size)
{
    size_t oldSize = v->size;
    size_t newSize;
    byte *newData;

    checkByteVector(v);
    size += v->size;
    if (size > v->allocatedSize)
    {
        newSize = v->allocatedSize;
        do
        {
            newSize *= 2;
            if (newSize < v->allocatedSize)
            {
                return null;
            }
        }
        while (size > newSize);
        newData = (byte*)malloc(newSize);
        memcpy(newData, v->data, v->size);
        free(v->data);
        v->data = newData;
        v->allocatedSize = newSize;
    }
    v->size = size;
    return &v->data[oldSize];
}


bytevector *ByteVectorCreate(size_t reserveSize)
{
    bytevector *v = (bytevector*)malloc(sizeof(bytevector));
    ByteVectorInit(v, reserveSize);
    return v;
}

void ByteVectorInit(bytevector *v, size_t reserveSize)
{
    reserveSize = max(reserveSize, 4);
    v->data = (byte*)malloc(reserveSize);
    v->size = 0;
    v->allocatedSize = reserveSize;
}

void ByteVectorDispose(bytevector *v)
{
    free(v->data);
    v->data = null;
}

byte *ByteVectorDisposeContainer(bytevector *v)
{
    byte *data = v->data;
    v->data = null;
    return data;
}


size_t ByteVectorSize(const bytevector *v)
{
    checkByteVector(v);
    return v->size;
}

void ByteVectorSetSize(bytevector *v, size_t size)
{
    if (size < v->allocatedSize)
    {
        v->size = size;
    }
    else
    {
        grow(v, size - v->size);
    }
}

void ByteVectorGrow(bytevector *v, size_t size)
{
    grow(v, size);
}

void ByteVectorGrowZero(bytevector *v, size_t size)
{
    byte *p = grow(v, size);
    memset(p, 0, size);
}

void ByteVectorReserveSize(bytevector *v, size_t size)
{
    if (size > v->allocatedSize)
    {
        ByteVectorReserveAppendSize(v, size - v->size);
    }
}

void ByteVectorReserveAppendSize(bytevector *v, size_t size)
{
    size_t oldSize = v->size;
    grow(v, size);
    v->size = oldSize;
}

size_t ByteVectorGetReservedAppendSize(const bytevector *v)
{
    checkByteVector(v);
    return v->allocatedSize - v->size;
}


byte *ByteVectorGetPointer(const bytevector *v, size_t index)
{
    if (index)
    {
        checkByteVectorIndex(v, index - 1);
    }
    else
    {
        checkByteVector(v);
    }
    return &v->data[index];
}

byte *ByteVectorGetAppendPointer(bytevector *v)
{
    checkByteVector(v);
    return &v->data[v->size];
}


void ByteVectorCopy(const bytevector *src, size_t srcOffset,
                    bytevector *dst, size_t dstOffset, size_t size)
{
    checkByteVectorRange(src, srcOffset, size);
    checkByteVectorRange(dst, dstOffset, size);
    memmove(&dst->data[dstOffset], &src->data[srcOffset], size);
}

void ByteVectorMove(bytevector *v, size_t src, size_t dst, size_t size)
{
    checkByteVectorRange(v, src, size);
    checkByteVectorRange(v, dst, size);
    memmove(&v->data[dst], &v->data[src], size);
}

void ByteVectorFill(bytevector *v, size_t index, size_t size, byte value)
{
    checkByteVectorRange(v, index, size);
    memset(&v->data[index], value, size);
}


void ByteVectorAppend(const bytevector *src, size_t srcOffset,
                      bytevector *dst, size_t size)
{
    byte *p;
    checkByteVectorRange(src, srcOffset, size);
    p = grow(dst, size);
    memcpy(p, ByteVectorGetPointer(src, srcOffset), size);
}

void ByteVectorAppendAll(const bytevector *restrict src,
                         bytevector *restrict dst)
{
    ByteVectorAppend(src, 0, dst, ByteVectorSize(src));
}


void ByteVectorAdd(bytevector *v, byte value)
{
    byte *p = grow(v, 1);
    *p = value;
}

void ByteVectorAddInt(bytevector *v, int value)
{
    ByteVectorAddUint(v, (uint)value);
}

void ByteVectorAddUint(bytevector *v, uint value)
{
    uint *p = (uint*)grow(v, sizeof(uint));
    *p = value;
}

void ByteVectorAddUint16(bytevector *v, uint16 value)
{
    byte *p = grow(v, sizeof(uint16));
    *p++ = (byte)(value >> 16);
    *p = (byte)value;
}

void ByteVectorAddRef(bytevector *v, ref_t value)
{
    ByteVectorAddUint(v, uintFromRef(value));
}

void ByteVectorAddData(bytevector *v, const byte *data, size_t size)
{
    byte *p = grow(v, size);
    memcpy(p, data, size);
}

void ByteVectorInsertData(bytevector *v, size_t offset,
                          const byte *data, size_t size)
{
    byte *p = grow(v, size);
    byte *insert = ByteVectorGetPointer(v, offset);
    memmove(insert + size, insert, (size_t)(p - insert));
    memcpy(insert, data, size);
}


byte ByteVectorGet(const bytevector *v, size_t index)
{
    checkByteVectorIndex(v, index);
    return v->data[index];
}

uint ByteVectorGetUint(const bytevector *v, size_t index)
{
    checkByteVectorRange(v, index, sizeof(int));
    return *(uint*)&v->data[index];
}

uint16 ByteVectorGetUint16(const bytevector *v, size_t index)
{
    checkByteVectorRange(v, index, sizeof(uint16));
    return (uint16)(((uint16)v->data[index] << 8) + v->data[index + 1]);
}


void ByteVectorSet(bytevector *v, size_t index, byte value)
{
    checkByteVectorIndex(v, index);
    v->data[index] = value;
}

void ByteVectorSetInt(bytevector *v, size_t index, int value)
{
    checkByteVectorRange(v, index, sizeof(int));
    *((int*)&v->data[index]) = value;
}

void ByteVectorSetUint(bytevector *v, size_t index, uint value)
{
    checkByteVectorRange(v, index, sizeof(uint));
    *((uint*)&v->data[index]) = value;
}


byte ByteVectorRead(const bytevector *v, size_t *index)
{
    checkByteVectorIndex(v, *index);
    return v->data[(*index)++];
}

uint ByteVectorReadUint(const bytevector *v, size_t *index)
{
    uint value;
    checkByteVectorRange(v, *index, sizeof(int));
    value = *(uint*)&v->data[*index];
    *index += sizeof(int);
    return value;
}

uint16 ByteVectorReadUint16(const bytevector *v, size_t *index)
{
    uint16 value;
    checkByteVectorRange(v, *index, sizeof(uint16));
    value = (uint16)((v->data[*index] << 8) + v->data[*index + 1]);
    *index += sizeof(uint16);
    return value;
}


void ByteVectorWrite(bytevector *v, size_t *index, byte value)
{
    checkByteVectorIndex(v, *index);
    v->data[(*index)++] = value;
}

void ByteVectorWriteInt(bytevector *v, size_t *index, int value)
{
    ByteVectorSetInt(v, *index, value);
    *index += sizeof(int);
}

void ByteVectorWriteUint(bytevector *v, size_t *index, uint value)
{
    ByteVectorSetUint(v, *index, value);
    *index += sizeof(int);
}


byte ByteVectorPeek(const bytevector *v)
{
    checkByteVectorIndex(v, 0);
    return v->data[v->size - 1];
}

byte ByteVectorPop(bytevector *v)
{
    checkByteVectorIndex(v, 0);
    v->size--;
    return v->data[v->size];
}

void ByteVectorPopData(bytevector *v, byte *value, size_t size)
{
    checkByteVectorRange(v, 0, size);
    v->size -= size;
    memcpy(value, &v->data[v->size], size);
}
