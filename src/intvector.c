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
    assert(index < v->size || (index == v->size && !length));
    assert(IntVectorSize(v) >= index + length);
}

ErrorCode IntVectorInit(intvector *v)
{
    v->data = (uint*)malloc(SEGMENT_SIZE * sizeof(uint));
    if (!v->data)
    {
        return OUT_OF_MEMORY;
    }
    v->size = 0;
    return NO_ERROR;
}

ErrorCode IntVectorInitCopy(intvector *restrict v,
                            const intvector *restrict data)
{
    v->data = (uint*)malloc(SEGMENT_SIZE * sizeof(uint));
    if (!v->data)
    {
        return OUT_OF_MEMORY;
    }
    memcpy(v->data, data->data, data->size * sizeof(uint));
    v->size = data->size;
    return NO_ERROR;
}

void IntVectorDispose(intvector *v)
{
    free(v->data);
    v->data = null;
}

uint IntVectorSize(const intvector *v)
{
    checkIntVector(v);
    return v->size;
}

ErrorCode IntVectorSetSize(intvector *v, uint size)
{
    checkIntVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow int vector */
    v->size = size;
    return NO_ERROR;
}

ErrorCode IntVectorGrowZero(intvector *v, uint size)
{
    uint oldSize = IntVectorSize(v);
    ErrorCode error = IntVectorSetSize(v, oldSize + size);
    if (error)
    {
        return error;
    }
    memset((void*)IntVectorGetPointer(v, oldSize), 0, size * sizeof(int));
    return NO_ERROR;
}

void IntVectorCopy(const intvector *restrict src, uint srcOffset,
                   intvector *restrict dst, uint dstOffset, uint length)
{
    checkIntVectorRange(src, srcOffset, length);
    checkIntVectorRange(dst, dstOffset, length);
    memmove(&dst->data[dstOffset], &src->data[srcOffset],
            length * sizeof(uint));
}

ErrorCode IntVectorAppend(const intvector *restrict src, uint srcOffset,
                          intvector *restrict dst, uint length)
{
    uint size = IntVectorSize(dst);
    ErrorCode error = IntVectorSetSize(dst, size + length);
    if (error)
    {
        return error;
    }
    IntVectorCopy(src, srcOffset, dst, size, length);
    return NO_ERROR;
}

ErrorCode IntVectorAppendAll(const intvector *restrict src,
                             intvector *restrict dst)
{
    return IntVectorAppend(src, 0, dst, IntVectorSize(src));
}

void IntVectorMove(intvector *v, uint src, uint dst, uint length)
{
    checkIntVectorRange(v, src, length);
    checkIntVectorRange(v, dst, length);
    memmove(&v->data[dst], &v->data[src], length * sizeof(uint));
}

ErrorCode IntVectorAdd(intvector *v, uint value)
{
    checkIntVector(v);
    assert(IntVectorSize(v) + 1 < SEGMENT_SIZE); /* TODO: grow int vector */
    v->data[v->size++] = value;
    return NO_ERROR;
}

ErrorCode IntVectorAdd4(intvector *v, uint value1, uint value2, uint value3,
                        uint value4)
{
    uint size = IntVectorSize(v);
    uint *p;
    ErrorCode error;
    checkIntVector(v);
    error = IntVectorSetSize(v, size + 4);
    if (error)
    {
        return error;
    }
    p = &v->data[size];
    *p++ = value1;
    *p++ = value2;
    *p++ = value3;
    *p++ = value4;
    return NO_ERROR;
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

uint IntVectorPeek(const intvector *v)
{
    checkIntVectorIndex(v, 0);
    return v->data[v->size - 1];
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
