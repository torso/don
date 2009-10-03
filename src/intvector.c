#include <stdlib.h>
#include "builder.h"
#include "intvector.h"

#define SEGMENT_SIZE 1024

static void checkIntVector(const intvector* v)
{
    assert(v);
    assert(v->data);
}

static void checkIntVectorIndex(const intvector* v, uint index)
{
    checkIntVector(v);
    assert(index < v->size);
}

void IntVectorInit(intvector* v)
{
    v->data = malloc(SEGMENT_SIZE * sizeof(int));
    assert(v->data); /* TODO: handle oom */
    v->size = 0;
}

void IntVectorFree(intvector* v)
{
    free(v->data);
}

uint IntVectorSize(const intvector* v)
{
    checkIntVector(v);
    return v->size;
}

void IntVectorAdd(intvector* v, int value)
{
    checkIntVector(v);
    assert(IntVectorSize(v) + 1 < SEGMENT_SIZE); /* TODO: grow int vector */
    v->data[v->size++] = value;
}

void IntVectorAdd4(intvector* v, int value1, int value2, int value3, int value4)
{
    checkIntVector(v);
    IntVectorAdd(v, value1);
    IntVectorAdd(v, value2);
    IntVectorAdd(v, value3);
    IntVectorAdd(v, value4);
}

int IntVectorGet(const intvector* v, uint index)
{
    checkIntVectorIndex(v, index);
    return v->data[index];
}

const int *IntVectorGetPointer(const intvector* v, uint index)
{
    checkIntVectorIndex(v, index);
    return &v->data[index];
}

void IntVectorSet(intvector* v, uint index, int value)
{
    checkIntVectorIndex(v, index);
    v->data[index] = value;
}
