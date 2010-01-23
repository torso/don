#include <stdlib.h>
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

void ByteVectorInit(bytevector *v)
{
    v->data = (byte*)malloc(SEGMENT_SIZE);
    assert(v->data); /* TODO: handle oom */
    v->size = 0;
}

void ByteVectorFree(bytevector *v)
{
    free(v->data);
}

uint ByteVectorSize(const bytevector *v)
{
    checkByteVector(v);
    return v->size;
}

void ByteVectorSetSize(bytevector *v, uint size)
{
    checkByteVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow byte vector */
    v->size = size;
}

boolean ByteVectorAdd(bytevector *v, byte value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + 1 < SEGMENT_SIZE); /* TODO: grow byte vector */
    v->data[v->size++] = value;
    return true;
}

boolean ByteVectorAddInt(bytevector *v, int value)
{
    return ByteVectorAddUint(v, (uint)value);
}

boolean ByteVectorAddUint(bytevector *v, uint value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + 4 < SEGMENT_SIZE); /* TODO: grow byte vector */
    *((uint*)&v->data[v->size]) = value;
    v->size += 4;
    return true;
}

boolean ByteVectorAddPackInt(bytevector *v, int value)
{
    return ByteVectorAddPackUint(v, (uint)value);
}

boolean ByteVectorAddPackUint(bytevector *v, uint value)
{
    checkByteVector(v);
    if (value <= 127)
    {
        return ByteVectorAdd(v, (byte)value);
    }
    v->data[v->size++] = 128;
    *((uint*)&v->data[v->size]) = value;
    v->size += 4;
    return true;
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
    checkByteVectorIndex(v, index);
    checkByteVectorIndex(v, index + (uint)sizeof(int) - 1);
    return *(uint*)&v->data[index];
}

uint ByteVectorReadUint(const bytevector *v, uint *index)
{
    uint value;
    checkByteVectorIndex(v, *index);
    checkByteVectorIndex(v, *index + (uint)sizeof(int) - 1);
    value = *(uint*)&v->data[*index];
    *index += (uint)sizeof(int);
    return value;
}

int ByteVectorGetPackInt(const bytevector *v, uint index)
{
    int i;
    checkByteVectorIndex(v, index);
    i = (int8)v->data[index];
    if (i < 0)
    {
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
        checkByteVectorIndex(v, *index + (uint)sizeof(int));
        value = *((uint*)&v->data[*index + 1]);
        *index += 1 + (uint)sizeof(int);
        return value;
    }
    (*index)++;
    return (uint)i;
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
    checkByteVectorIndex(v, index);
    checkByteVectorIndex(v, index + 3);
    *((int*)&v->data[index]) = value;
}

void ByteVectorSetUint(bytevector *v, uint index, uint value)
{
    checkByteVectorIndex(v, index);
    checkByteVectorIndex(v, index + 3);
    *((uint*)&v->data[index]) = value;
}

void ByteVectorSetPackUint(bytevector *v, uint index, uint value)
{
    assert(ByteVectorGetPackUint(v, index) >= value);
    if ((int8)v->data[index] < 0)
    {
        *((uint*)&v->data[index + 1]) = value;
    }
    else
    {
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

void ByteVectorFill(bytevector *v, uint index, uint size, byte value)
{
    if (size == 0)
    {
        return;
    }
    checkByteVectorIndex(v, index);
    checkByteVectorIndex(v, index + size - 1);
    assert((index & ~(SEGMENT_SIZE - 1)) == ((index + size) & ~(SEGMENT_SIZE - 1))); /* TODO: big byte vector support */
    memset(&v->data[index], value, size);
}
