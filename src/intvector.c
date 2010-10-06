#include <memory.h>
#include "common.h"
#include "intvector.h"

#define SEGMENT_SIZE (size_t)1024

static void checkIntVector(const intvector *v)
{
    assert(v);
    assert(v->data);
}

static void checkIntVectorIndex(const intvector *v, size_t index)
{
    checkIntVector(v);
    assert(index < v->size);
}

static void checkIntVectorRange(const intvector *v, size_t index, size_t size)
{
    checkIntVector(v);
    assert(index < v->size || (index == v->size && !size));
    assert(IntVectorSize(v) >= index + size);
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

size_t IntVectorSize(const intvector *v)
{
    checkIntVector(v);
    return v->size;
}

ErrorCode IntVectorSetSize(intvector *v, size_t size)
{
    checkIntVector(v);
    assert(size <= SEGMENT_SIZE); /* TODO: grow int vector */
    v->size = size;
    return NO_ERROR;
}

ErrorCode IntVectorGrowZero(intvector *v, size_t size)
{
    size_t oldSize = IntVectorSize(v);
    ErrorCode error = IntVectorSetSize(v, oldSize + size);
    if (error)
    {
        return error;
    }
    memset((void*)IntVectorGetPointer(v, oldSize), 0, size * sizeof(int));
    return NO_ERROR;
}

void IntVectorCopy(const intvector *restrict src, size_t srcOffset,
                   intvector *restrict dst, size_t dstOffset, size_t size)
{
    checkIntVectorRange(src, srcOffset, size);
    checkIntVectorRange(dst, dstOffset, size);
    memmove(&dst->data[dstOffset], &src->data[srcOffset],
            size * sizeof(uint));
}

ErrorCode IntVectorAppend(const intvector *restrict src, size_t srcOffset,
                          intvector *restrict dst, size_t size)
{
    size_t vectorSize = IntVectorSize(dst);
    ErrorCode error = IntVectorSetSize(dst, vectorSize + size);
    if (error)
    {
        return error;
    }
    IntVectorCopy(src, srcOffset, dst, size, size);
    return NO_ERROR;
}

ErrorCode IntVectorAppendAll(const intvector *restrict src,
                             intvector *restrict dst)
{
    return IntVectorAppend(src, 0, dst, IntVectorSize(src));
}

void IntVectorMove(intvector *v, size_t src, size_t dst, size_t size)
{
    checkIntVectorRange(v, src, size);
    checkIntVectorRange(v, dst, size);
    memmove(&v->data[dst], &v->data[src], size * sizeof(uint));
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
    size_t size = IntVectorSize(v);
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

uint IntVectorGet(const intvector *v, size_t index)
{
    checkIntVectorIndex(v, index);
    return v->data[index];
}

const uint *IntVectorGetPointer(const intvector *v, size_t index)
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

void IntVectorSet(intvector *v, size_t index, uint value)
{
    checkIntVectorIndex(v, index);
    v->data[index] = value;
}
