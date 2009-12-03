#include <stdlib.h>
#include <memory.h>
#include "builder.h"
#include "bytevector.h"

#define SEGMENT_SIZE 1024

static void checkByteVector(const bytevector* v)
{
    assert(v);
    assert(v->data);
}

static void checkByteVectorIndex(const bytevector* v, uint index)
{
    checkByteVector(v);
    assert(index < v->size);
}

void ByteVectorInit(bytevector* v)
{
    v->data = malloc(SEGMENT_SIZE);
    assert(v->data); /* TODO: handle oom */
    v->size = 0;
}

void ByteVectorFree(bytevector* v)
{
    free(v->data);
}

uint ByteVectorSize(const bytevector* v)
{
    checkByteVector(v);
    return v->size;
}

void ByteVectorSetSize(bytevector* v, uint size)
{
    checkByteVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow byte vector */
    v->size = size;
}

boolean ByteVectorAdd(bytevector* v, byte value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + 1 < SEGMENT_SIZE); /* TODO: grow byte vector */
    v->data[v->size++] = value;
    return true;
}

boolean ByteVectorAddInt(bytevector* v, int value)
{
    checkByteVector(v);
    assert(ByteVectorSize(v) + 4 < SEGMENT_SIZE); /* TODO: grow byte vector */
    *((int*)&v->data[v->size]) = value;
    v->size += 4;
    return true;
}

boolean ByteVectorAddPackUint(bytevector* v, uint value)
{
    checkByteVector(v);
    if (value <= 127)
    {
        return ByteVectorAdd(v, value);
    }
    v->data[v->size++] = 128;
    *((uint*)&v->data[v->size]) = value;
    v->size += 4;
    return true;
}

byte ByteVectorGet(const bytevector* v, uint index)
{
    checkByteVectorIndex(v, index);
    return v->data[index];
}

int ByteVectorGetInt(const bytevector* v, uint index)
{
    return ByteVectorGetUint(v, index);
}

uint ByteVectorGetUint(const bytevector* v, uint index)
{
    checkByteVectorIndex(v, index);
    checkByteVectorIndex(v, index + 3);
    return *(uint*)&v->data[index];
}

int ByteVectorGetPackInt(const bytevector* v, uint index)
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

uint ByteVectorGetPackUint(const bytevector* v, uint index)
{
    int i;
    checkByteVectorIndex(v, index);
    i = (int8)v->data[index];
    if (i < 0)
    {
        return *((uint*)&v->data[index + 1]);
    }
    return i;
}

uint ByteVectorGetPackUintSize(const bytevector* v, uint index)
{
    checkByteVectorIndex(v, index);
    return (int8)v->data[index] < 0 ? 5 : 1;
}

const byte* ByteVectorGetPointer(const bytevector* v, uint index)
{
    checkByteVectorIndex(v, index);
    return &v->data[index];
}

void ByteVectorSet(bytevector* v, uint index, byte value)
{
    checkByteVectorIndex(v, index);
    v->data[index] = value;
}

void ByteVectorSetInt(bytevector* v, uint index, int value)
{
    checkByteVectorIndex(v, index);
    checkByteVectorIndex(v, index + 3);
    *((int*)&v->data[index]) = value;
}

void ByteVectorFill(bytevector* v, uint index, uint size, byte value)
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
