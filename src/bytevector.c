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
        if (!newData)
        {
            return null;
        }
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
    if (!v)
    {
        return null;
    }
    if (ByteVectorInit(v, reserveSize))
    {
        free(v);
        return null;
    }
    return v;
}

ErrorCode ByteVectorInit(bytevector *v, size_t reserveSize)
{
    reserveSize = max(reserveSize, 4);
    v->data = (byte*)malloc(reserveSize);
    if (!v->data)
    {
        return OUT_OF_MEMORY;
    }
    v->size = 0;
    v->allocatedSize = reserveSize;
    return NO_ERROR;
}

void ByteVectorDispose(bytevector *v)
{
    free(v->data);
    v->data = null;
}

byte *ByteVectorDisposeContainer(bytevector *v)
{
    return v->data;
}


size_t ByteVectorSize(const bytevector *v)
{
    checkByteVector(v);
    return v->size;
}

ErrorCode ByteVectorSetSize(bytevector *v, size_t size)
{
    if (size < v->allocatedSize)
    {
        v->size = size;
    }
    else if (!grow(v, size - v->size))
    {
        return OUT_OF_MEMORY;
    }
    return NO_ERROR;
}

ErrorCode ByteVectorGrow(bytevector *v, size_t size)
{
    if (!grow(v, size))
    {
        return OUT_OF_MEMORY;
    }
    return NO_ERROR;
}

ErrorCode ByteVectorGrowZero(bytevector *v, size_t size)
{
    byte *p = grow(v, size);
    if (!p)
    {
        return OUT_OF_MEMORY;
    }
    memset(p, 0, size);
    return NO_ERROR;
}

ErrorCode ByteVectorReserveSize(bytevector *v, size_t size)
{
    if (size <= v->allocatedSize)
    {
        return NO_ERROR;
    }
    return ByteVectorReserveSize(v, v->size + size);
}

ErrorCode ByteVectorReserveAppendSize(bytevector *v, size_t size)
{
    size_t oldSize = v->size;
    if (!grow(v, size))
    {
        return OUT_OF_MEMORY;
    }
    v->size = oldSize;
    return NO_ERROR;
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


ErrorCode ByteVectorAppend(const bytevector *src, size_t srcOffset,
                           bytevector *dst, size_t size)
{
    byte *p;

    checkByteVectorRange(src, srcOffset, size);
    p = grow(dst, size);
    if (!p)
    {
        return OUT_OF_MEMORY;
    }
    memcpy(p, ByteVectorGetPointer(src, srcOffset), size);
    return NO_ERROR;
}

ErrorCode ByteVectorAppendAll(const bytevector *restrict src,
                              bytevector *restrict dst)
{
    return ByteVectorAppend(src, 0, dst, ByteVectorSize(src));
}


ErrorCode ByteVectorAdd(bytevector *v, byte value)
{
    byte *p = grow(v, 1);
    if (!p)
    {
        return OUT_OF_MEMORY;
    }
    *p = value;
    return NO_ERROR;
}

ErrorCode ByteVectorAddInt(bytevector *v, int value)
{
    return ByteVectorAddUint(v, (uint)value);
}

ErrorCode ByteVectorAddUint(bytevector *v, uint value)
{
    uint *p = (uint*)grow(v, sizeof(uint));
    if (!p)
    {
        return OUT_OF_MEMORY;
    }
    *p = value;
    return NO_ERROR;
}

ErrorCode ByteVectorAddUint16(bytevector *v, uint16 value)
{
    byte *p = grow(v, sizeof(uint16));
    if (!p)
    {
        return OUT_OF_MEMORY;
    }
    *p++ = (byte)(value >> 16);
    *p = (byte)value;
    return NO_ERROR;
}

ErrorCode ByteVectorAddRef(bytevector *v, ref_t value)
{
    return ByteVectorAddUint(v, uintFromRef(value));
}

ErrorCode ByteVectorAddData(bytevector *v, const byte *value, size_t size)
{
    byte *p = grow(v, size);
    if (!p)
    {
        return OUT_OF_MEMORY;
    }
    memcpy(p, value, size);
    return NO_ERROR;
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
