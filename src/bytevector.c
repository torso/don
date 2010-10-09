#include <memory.h>
#include "common.h"
#include "bytevector.h"

#define SEGMENT_SIZE (size_t)1024

static void checkByteVector(const bytevector *v)
{
    assert(v);
    assert(v->data);
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


bytevector *ByteVectorCreate(void)
{
    bytevector *v = (bytevector*)malloc(sizeof(bytevector));
    if (!v)
    {
        return null;
    }
    if (ByteVectorInit(v))
    {
        free(v);
        return null;
    }
    return v;
}

ErrorCode ByteVectorInit(bytevector *v)
{
    v->data = (byte*)malloc(SEGMENT_SIZE);
    if (!v->data)
    {
        return OUT_OF_MEMORY;
    }
    v->size = 0;
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
    checkByteVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow byte vector */
    v->size = size;
    return NO_ERROR;
}

ErrorCode ByteVectorGrow(bytevector *v, size_t size)
{
    return ByteVectorSetSize(v, ByteVectorSize(v) + size);
}

ErrorCode ByteVectorGrowZero(bytevector *v, size_t size)
{
    size_t oldSize = ByteVectorSize(v);
    ErrorCode error = ByteVectorSetSize(v, oldSize + size);
    if (error)
    {
        return error;
    }
    memset((void*)ByteVectorGetPointer(v, oldSize), 0, size);
    return NO_ERROR;
}

void ByteVectorCopy(const bytevector *restrict src, size_t srcOffset,
                    bytevector *restrict dst, size_t dstOffset, size_t size)
{
    checkByteVectorRange(src, srcOffset, size);
    checkByteVectorRange(dst, dstOffset, size);
    memmove(&dst->data[dstOffset], &src->data[srcOffset], size);
}

ErrorCode ByteVectorAppend(const bytevector *restrict src, size_t srcOffset,
                           bytevector *restrict dst, size_t size)
{
    size_t vectorSize = ByteVectorSize(dst);
    ErrorCode error = ByteVectorSetSize(dst, vectorSize + size);
    if (error)
    {
        return error;
    }
    ByteVectorCopy(src, srcOffset, dst, vectorSize, size);
    return NO_ERROR;
}

ErrorCode ByteVectorAppendAll(const bytevector *restrict src,
                              bytevector *restrict dst)
{
    return ByteVectorAppend(src, 0, dst, ByteVectorSize(src));
}

void ByteVectorMove(bytevector *v, size_t src, size_t dst, size_t size)
{
    checkByteVectorRange(v, src, size);
    checkByteVectorRange(v, dst, size);
    memmove(&v->data[dst], &v->data[src], size);
}

ErrorCode ByteVectorAdd(bytevector *v, byte value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + 1 < SEGMENT_SIZE); /* TODO: grow byte vector */
    v->data[v->size++] = value;
    return NO_ERROR;
}

ErrorCode ByteVectorAddInt(bytevector *v, int value)
{
    return ByteVectorAddUint(v, (uint)value);
}

ErrorCode ByteVectorAddUint(bytevector *v, uint value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + sizeof(uint) < SEGMENT_SIZE); /* TODO: grow byte vector */
    *((uint*)&v->data[v->size]) = value;
    v->size += 4;
    return NO_ERROR;
}

ErrorCode ByteVectorAddUint16(bytevector *v, uint16 value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + sizeof(uint16) < SEGMENT_SIZE); /* TODO: grow byte vector */
    v->data[v->size++] = (byte)(value >> 16);
    v->data[v->size++] = (byte)value;
    return NO_ERROR;
}

ErrorCode ByteVectorAddPackInt(bytevector *v, int value)
{
    return ByteVectorAddPackUint(v, (uint)value);
}

ErrorCode ByteVectorAddPackUint(bytevector *v, uint value)
{
    checkByteVector(v);
    if (value <= 127)
    {
        return ByteVectorAdd(v, (byte)value);
    }
    return ByteVectorAddUnpackedUint(v, value);
}

ErrorCode ByteVectorAddUnpackedInt(bytevector *v, int value)
{
    return ByteVectorAddUnpackedUint(v, (uint)value);
}

ErrorCode ByteVectorAddUnpackedUint(bytevector *v, uint value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + 5 < SEGMENT_SIZE); /* TODO: grow byte vector */
    v->data[v->size++] = 128;
    *((uint*)&v->data[v->size]) = value;
    v->size += 4;
    return NO_ERROR;
}

ErrorCode ByteVectorAddRef(bytevector *v, ref_t value)
{
    return ByteVectorAddUint(v, uintFromRef(value));
}

ErrorCode ByteVectorAddData(bytevector *v, const byte *value, size_t size)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + size < SEGMENT_SIZE); /* TODO: grow byte vector */
    memcpy(&v->data[v->size], value, size);
    v->size += size;
    return NO_ERROR;
}

byte ByteVectorGet(const bytevector *v, size_t index)
{
    checkByteVectorIndex(v, index);
    return v->data[index];
}

byte ByteVectorRead(const bytevector *v, size_t *index)
{
    checkByteVectorIndex(v, *index);
    return v->data[(*index)++];
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

int ByteVectorGetPackInt(const bytevector *v, size_t index)
{
    int i;
    checkByteVectorIndex(v, index);
    i = (int8)v->data[index];
    if (i < 0)
    {
        checkByteVectorRange(v, index, sizeof(int) + 1);
        return *((int*)&v->data[index + 1]);
    }
    return i;
}

uint ByteVectorGetPackUint(const bytevector *v, size_t index)
{
    return ByteVectorReadPackUint(v, &index);
}

uint ByteVectorReadPackUint(const bytevector *v, size_t *index)
{
    int i;
    uint value;
    checkByteVectorIndex(v, *index);
    i = (int8)v->data[*index];
    if (i < 0)
    {
        checkByteVectorRange(v, *index, sizeof(int) + 1);
        value = *((uint*)&v->data[*index + 1]);
        *index += 1 + sizeof(int);
        return value;
    }
    (*index)++;
    return (uint)i;
}

void ByteVectorSkipPackUint(const bytevector *v, size_t *index)
{
    *index += ByteVectorGetPackUintSize(v, *index);
}

uint ByteVectorGetPackUintSize(const bytevector *v, size_t index)
{
    checkByteVectorIndex(v, index);
    return (int8)v->data[index] < 0 ? 5 : 1;
}

const byte *ByteVectorGetPointer(const bytevector *v, size_t index)
{
    if (index)
    {
        checkByteVectorIndex(v, index);
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

size_t ByteVectorGetAvailableSize(const bytevector *v)
{
    return SEGMENT_SIZE - ByteVectorSize(v);
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

void ByteVectorSet(bytevector *v, size_t index, byte value)
{
    checkByteVectorIndex(v, index);
    v->data[index] = value;
}

void ByteVectorWrite(bytevector *v, size_t *index, byte value)
{
    checkByteVectorIndex(v, *index);
    v->data[(*index)++] = value;
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

void ByteVectorSetPackInt(bytevector *v, size_t index, int value)
{
    ByteVectorSetPackUint(v, index, (uint)value);
}

void ByteVectorSetPackUint(bytevector *v, size_t index, uint value)
{
    if ((int8)v->data[index] < 0)
    {
        *((uint*)&v->data[index + 1]) = value;
    }
    else
    {
        assert(value < 127);
        v->data[index] = (byte)value;
    }
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

void ByteVectorWritePackInt(bytevector *v, size_t *index, int value)
{
    ByteVectorWritePackUint(v, index, (uint)value);
}

void ByteVectorWritePackUint(bytevector *v, size_t *index, uint value)
{
    if (value <= 127)
    {
        ByteVectorWrite(v, index, (byte)value);
    }
    else
    {
        ByteVectorWrite(v, index, 128);
        ByteVectorWriteInt(v, index, (int)value);
    }
}

void ByteVectorFill(bytevector *v, size_t index, size_t size, byte value)
{
    checkByteVectorRange(v, index, size);
    assert((index & ~(SEGMENT_SIZE - 1)) == ((index + size) & ~(SEGMENT_SIZE - 1))); /* TODO: big byte vector support */
    memset(&v->data[index], value, size);
}
