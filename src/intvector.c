#include <stdlib.h>
#include <memory.h>
#include "builder.h"
#include "intvector.h"

#define SEGMENT_SIZE 1024

static void checkIntVector(const intvector *v)
{
    assert(v);
    assert(v->data);
}

static void checkIntVectorIndex(const intvector *v, uint index)
{
    checkIntVector(v);
    assert(index < v->size);
}

static void checkIntVectorRange(const intvector *v, uint index, uint length)
{
    checkIntVector(v);
    assert(index < v->size);
    assert(IntVectorSize(v) >= index + length);
}

void IntVectorInit(intvector *v)
{
    v->data = (uint*)malloc(SEGMENT_SIZE * sizeof(uint));
    assert(v->data); /* TODO: handle oom */
    v->size = 0;
}

void IntVectorInitCopy(intvector *restrict v, const intvector *restrict data)
{
    v->data = (uint*)malloc(SEGMENT_SIZE * sizeof(uint));
    assert(v->data); /* TODO: handle oom */
    memcpy(v->data, data->data, data->size * sizeof(uint));
    v->size = data->size;
}

void IntVectorFree(intvector *v)
{
    free(v->data);
}

uint IntVectorSize(const intvector *v)
{
    checkIntVector(v);
    return v->size;
}

void IntVectorSetSize(intvector *v, uint size)
{
    checkIntVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow int vector */
    v->size = size;
}

void IntVectorCopy(const intvector *restrict src, uint srcOffset,
                   intvector *restrict dst, uint dstOffset, uint length)
{
    checkIntVectorRange(src, srcOffset, length);
    checkIntVectorRange(dst, dstOffset, length);
    memcpy(&dst->data[dstOffset], &src->data[srcOffset], length * sizeof(uint));
}

void IntVectorAppend(const intvector *restrict src, uint srcOffset,
                     intvector *restrict dst, uint length)
{
    uint size = IntVectorSize(dst);
    IntVectorSetSize(dst, size + length);
    IntVectorCopy(src, srcOffset, dst, size, length);
}

void IntVectorAppendAll(const intvector *restrict src,
                        intvector *restrict dst)
{
    IntVectorAppend(src, 0, dst, IntVectorSize(src));
}

void IntVectorMove(intvector *v, uint src, uint dst, uint length)
{
    checkIntVectorRange(v, src, length);
    checkIntVectorRange(v, dst, length);
    memmove(&v->data[dst], &v->data[src], length * sizeof(uint));
}

void IntVectorAdd(intvector *v, uint value)
{
    checkIntVector(v);
    assert(IntVectorSize(v) + 1 < SEGMENT_SIZE); /* TODO: grow int vector */
    v->data[v->size++] = value;
}

void IntVectorAdd4(intvector *v, uint value1, uint value2, uint value3,
                   uint value4)
{
    checkIntVector(v);
    IntVectorAdd(v, value1);
    IntVectorAdd(v, value2);
    IntVectorAdd(v, value3);
    IntVectorAdd(v, value4);
}

uint IntVectorGet(const intvector *v, uint index)
{
    checkIntVectorIndex(v, index);
    return v->data[index];
}

const uint *IntVectorGetPointer(const intvector *v, uint index)
{
    checkIntVectorIndex(v, index);
    return &v->data[index];
}

uint IntVectorPop(intvector *v)
{
    checkIntVectorIndex(v, 0);
    v->size--;
    return v->data[v->size];
}

void IntVectorSet(intvector *v, uint index, uint value)
{
    checkIntVectorIndex(v, index);
    v->data[index] = value;
}
