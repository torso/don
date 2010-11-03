#include <memory.h>
#include "common.h"
#include "intvector.h"

static void checkIntVector(const intvector *v)
{
    assert(v);
    assert(v->data);
    assert(v->allocatedSize);
    assert(v->allocatedSize >= v->size);
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

static uint *grow(intvector *v, size_t size)
{
    size_t oldSize = v->size;
    size_t newSize;

    checkIntVector(v);
    size += v->size;
    if (size > v->allocatedSize)
    {
        newSize = v->allocatedSize;
        do
        {
            newSize *= 2;
            assert(newSize >= v->allocatedSize); /* TODO: Handle error. */
        }
        while (size > newSize);
        v->data = (uint*)realloc(v->data, newSize * sizeof(uint));
        v->allocatedSize = newSize;
    }
    v->size = size;
    return &v->data[oldSize];
}


void IntVectorInit(intvector *v)
{
    size_t reserveSize = 4;
    v->data = (uint*)malloc(reserveSize * sizeof(uint));
    v->size = 0;
    v->allocatedSize = reserveSize;
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

void IntVectorSetSize(intvector *v, size_t size)
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

void IntVectorGrow(intvector *v, size_t size)
{
    grow(v, size);
}

void IntVectorGrowZero(intvector *v, size_t size)
{
    memset(grow(v, size), 0, size * sizeof(uint));
}


const uint *IntVectorGetPointer(const intvector *v, size_t index)
{
    checkIntVectorIndex(v, index);
    return &v->data[index];
}


void IntVectorCopy(const intvector *src, size_t srcOffset,
                   intvector *dst, size_t dstOffset, size_t size)
{
    checkIntVectorRange(src, srcOffset, size);
    checkIntVectorRange(dst, dstOffset, size);
    memmove(&dst->data[dstOffset], &src->data[srcOffset], size * sizeof(uint));
}

void IntVectorMove(intvector *v, size_t src, size_t dst, size_t size)
{
    checkIntVectorRange(v, src, size);
    checkIntVectorRange(v, dst, size);
    memmove(&v->data[dst], &v->data[src], size * sizeof(uint));
}

void IntVectorZero(intvector *v, size_t offset, size_t size)
{
    checkIntVectorRange(v, offset, size);
    memset(&v->data[offset], 0, size * sizeof(uint));
}


void IntVectorAppend(const intvector *src, size_t srcOffset,
                          intvector *dst, size_t size)
{
    uint *p;
    checkIntVectorRange(src, srcOffset, size);
    p = grow(dst, size);
    memcpy(p, IntVectorGetPointer(src, srcOffset), size * sizeof(uint));
}

void IntVectorAppendAll(const intvector *src, intvector *dst)
{
    IntVectorAppend(src, 0, dst, IntVectorSize(src));
}


void IntVectorAdd(intvector *v, uint value)
{
    uint *p = grow(v, 1);
    *p = value;
}

void IntVectorAddRef(intvector *v, ref_t value)
{
    IntVectorAdd(v, uintFromRef(value));
}


uint IntVectorGet(const intvector *v, size_t index)
{
    checkIntVectorIndex(v, index);
    return v->data[index];
}

ref_t IntVectorGetRef(const intvector *v, size_t index)
{
    return refFromUint(IntVectorGet(v, index));
}


void IntVectorSet(intvector *v, size_t index, uint value)
{
    checkIntVectorIndex(v, index);
    v->data[index] = value;
}

void IntVectorSetRef(intvector *v, size_t index, ref_t value)
{
    IntVectorSet(v, index, uintFromRef(value));
}


uint IntVectorPeek(const intvector *v)
{
    checkIntVectorIndex(v, 0);
    return v->data[v->size - 1];
}

ref_t IntVectorPeekRef(const intvector *v)
{
    return refFromUint(IntVectorPeek(v));
}

uint IntVectorPop(intvector *v)
{
    checkIntVectorIndex(v, 0);
    v->size--;
    return v->data[v->size];
}

ref_t IntVectorPopRef(intvector *v)
{
    return refFromUint(IntVectorPop(v));
}
