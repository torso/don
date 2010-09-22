#include <memory.h>
#include "builder.h"
#include "bytevector.h"

#define SEGMENT_SIZE (uint)1024

static void checkByteVector(const bytevector *v)
{
    assert(v);
    assert(v->data);
}

static void checkByteVectorIndex(const bytevector *v, uint index)
{
    checkByteVector(v);
    assert(index < v->size);
}

static void checkByteVectorRange(const bytevector *v, uint index, uint length)
{
    checkByteVector(v);
    assert(index < v->size || (index == v->size && !length));
    assert(ByteVectorSize(v) >= index + length);
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

uint ByteVectorSize(const bytevector *v)
{
    checkByteVector(v);
    return v->size;
}

ErrorCode ByteVectorSetSize(bytevector *v, uint size)
{
    checkByteVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow byte vector */
    v->size = size;
    return NO_ERROR;
}

void ByteVectorCopy(const bytevector *restrict src, uint srcOffset,
                    bytevector *restrict dst, uint dstOffset, uint length)
{
    checkByteVectorRange(src, srcOffset, length);
    checkByteVectorRange(dst, dstOffset, length);
    memmove(&dst->data[dstOffset], &src->data[srcOffset], length);
}

ErrorCode ByteVectorAppend(const bytevector *restrict src, uint srcOffset,
                           bytevector *restrict dst, uint length)
{
    uint size = ByteVectorSize(dst);
    ErrorCode error = ByteVectorSetSize(dst, size + length);
    if (error)
    {
        return error;
    }
    ByteVectorCopy(src, srcOffset, dst, size, length);
    return NO_ERROR;
}

ErrorCode ByteVectorAppendAll(const bytevector *restrict src,
                              bytevector *restrict dst)
{
    return ByteVectorAppend(src, 0, dst, ByteVectorSize(src));
}

void ByteVectorMove(bytevector *v, uint src, uint dst, uint length)
{
    checkByteVectorRange(v, src, length);
    checkByteVectorRange(v, dst, length);
    memmove(&v->data[dst], &v->data[src], length);
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

ErrorCode ByteVectorAddData(bytevector *v, byte *value, uint size)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + size < SEGMENT_SIZE); /* TODO: grow byte vector */
    memcpy(&v->data[v->size], value, size);
    v->size += size;
    return NO_ERROR;
}

byte ByteVectorGet(const bytevector *v, uint index)
{
    checkByteVectorIndex(v, index);
    return v->data[index];
}

byte ByteVectorRead(const bytevector *v, uint *index)
{
    checkByteVectorIndex(v, *index);
    return v->data[(*index)++];
}

uint ByteVectorGetUint(const bytevector *v, uint index)
{
    checkByteVectorRange(v, index, (uint)sizeof(int));
    return *(uint16*)&v->data[index];
}

uint16 ByteVectorGetUint16(const bytevector *v, uint index)
{
    checkByteVectorRange(v, index, (uint)sizeof(uint16));
    return (uint16)(((uint16)v->data[index] << 8) + v->data[index + 1]);
}

uint ByteVectorReadUint(const bytevector *v, uint *index)
{
    uint value;
    checkByteVectorRange(v, *index, (uint)sizeof(int));
    value = *(uint*)&v->data[*index];
    *index += (uint)sizeof(int);
    return value;
}

uint16 ByteVectorReadUint16(const bytevector *v, uint *index)
{
    uint16 value;
    checkByteVectorRange(v, *index, (uint)sizeof(uint16));
    value = (uint16)((v->data[*index] << 8) + v->data[*index + 1]);
    *index += (uint)sizeof(uint16);
    return value;
}

int ByteVectorGetPackInt(const bytevector *v, uint index)
{
    int i;
    checkByteVectorIndex(v, index);
    i = (int8)v->data[index];
    if (i < 0)
    {
        checkByteVectorRange(v, index, (uint)sizeof(int) + 1);
        return *((int*)&v->data[index + 1]);
    }
    return i;
}

uint ByteVectorGetPackUint(const bytevector *v, uint index)
{
    return ByteVectorReadPackUint(v, &index);
}

uint ByteVectorReadPackUint(const bytevector *v, uint *index)
{
    int i;
    uint value;
    checkByteVectorIndex(v, *index);
    i = (int8)v->data[*index];
    if (i < 0)
    {
        checkByteVectorRange(v, *index, (uint)sizeof(int) + 1);
        value = *((uint*)&v->data[*index + 1]);
        *index += 1 + (uint)sizeof(int);
        return value;
    }
    (*index)++;
    return (uint)i;
}

void ByteVectorSkipPackUint(const bytevector *v, uint *index)
{
    uint size = ByteVectorGetPackUintSize(v, *index);
    *index += size;
}

uint ByteVectorGetPackUintSize(const bytevector *v, uint index)
{
    checkByteVectorIndex(v, index);
    return (int8)v->data[index] < 0 ? 5 : 1;
}

const byte *ByteVectorGetPointer(const bytevector *v, uint index)
{
    checkByteVectorIndex(v, index);
    return &v->data[index];
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

void ByteVectorPopData(bytevector *v, byte *value, uint size)
{
    checkByteVectorRange(v, 0, size);
    v->size -= size;
    memcpy(value, &v->data[v->size], size);
}

void ByteVectorSet(bytevector *v, uint index, byte value)
{
    checkByteVectorIndex(v, index);
    v->data[index] = value;
}

void ByteVectorWrite(bytevector *v, uint *index, byte value)
{
    checkByteVectorIndex(v, *index);
    v->data[(*index)++] = value;
}

void ByteVectorSetInt(bytevector *v, uint index, int value)
{
    checkByteVectorRange(v, index, (uint)sizeof(int));
    *((int*)&v->data[index]) = value;
}

void ByteVectorSetUint(bytevector *v, uint index, uint value)
{
    checkByteVectorRange(v, index, (uint)sizeof(uint));
    *((uint*)&v->data[index]) = value;
}

void ByteVectorSetPackInt(bytevector *v, uint index, int value)
{
    ByteVectorSetPackUint(v, index, (uint)value);
}

void ByteVectorSetPackUint(bytevector *v, uint index, uint value)
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

void ByteVectorWriteInt(bytevector *v, uint *index, int value)
{
    ByteVectorSetInt(v, *index, value);
    *index += (uint)sizeof(int);
}

void ByteVectorWriteUint(bytevector *v, uint *index, uint value)
{
    ByteVectorSetUint(v, *index, value);
    *index += (uint)sizeof(int);
}

void ByteVectorWritePackInt(bytevector *v, uint *index, int value)
{
    ByteVectorWritePackUint(v, index, (uint)value);
}

void ByteVectorWritePackUint(bytevector *v, uint *index, uint value)
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

void ByteVectorFill(bytevector *v, uint index, uint length, byte value)
{
    checkByteVectorRange(v, index, length);
    assert((index & ~(SEGMENT_SIZE - 1)) == ((index + length) & ~(SEGMENT_SIZE - 1))); /* TODO: big byte vector support */
    memset(&v->data[index], value, length);
}
