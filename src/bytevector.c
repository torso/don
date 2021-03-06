#include "config.h"
#include <string.h>
#ifdef VALGRIND
#include <valgrind/memcheck.h>
#endif
#include "common.h"
#include "bytevector.h"

#define VECTOR_NAME bytevector
#define VECTOR_TYPE byte
#define VECTOR_FUNC(name) BV##name
#include "vector.inc"


void BVFill(bytevector *v, size_t index, size_t size, byte value)
{
    vectorCheckRange(v, index, size);
    memset(&v->data[index], value, size);
}


void BVAddInt(bytevector *v, int value)
{
    BVAddUint(v, (uint)value);
}

void BVAddUint(bytevector *v, uint value)
{
    uint *p = (uint*)vectorGrow(v, sizeof(value));
    *p = value;
}

void BVAddSize(bytevector *v, size_t value)
{
    size_t *p = (size_t*)vectorGrow(v, sizeof(value));
    *p = value;
}

void BVAddAll(bytevector *v, const bytevector *src)
{
    BVAddData(v, BVGetPointer(src, 0), BVSize(src));
}

void BVAddString(bytevector *v, const char *string)
{
    if (BVSize(v) && !BVPeek(v))
    {
        BVPop(v);
    }
    BVAddData(v, (const byte*)string, strlen(string));
}

void BVInsertData(bytevector *v, size_t offset,
                  const byte *data, size_t size)
{
    byte *p = vectorGrow(v, size);
    byte *insert = (byte*)BVGetPointer(v, offset);
    memmove(insert + size, insert, (size_t)(p - insert));
    memcpy(insert, data, size);
}


uint BVGetUint(const bytevector *v, size_t index)
{
    vectorCheckRange(v, index, sizeof(int));
    return *(uint*)&v->data[index];
}

void BVSetInt(bytevector *v, size_t index, int value)
{
    vectorCheckRange(v, index, sizeof(value));
    *((int*)&v->data[index]) = value;
}

void BVSetUint(bytevector *v, size_t index, uint value)
{
    vectorCheckRange(v, index, sizeof(value));
    *((uint*)&v->data[index]) = value;
}

void BVSetSizeAt(bytevector *v, size_t index, size_t value)
{
    vectorCheckRange(v, index, sizeof(value));
    *((size_t*)&v->data[index]) = value;
}


byte BVRead(const bytevector *v, size_t *index)
{
    vectorCheckIndex(v, *index);
    return v->data[(*index)++];
}

uint BVReadUint(const bytevector *v, size_t *index)
{
    uint value;
    vectorCheckRange(v, *index, sizeof(value));
    value = *(uint*)&v->data[*index];
    *index += sizeof(value);
    return value;
}

size_t BVReadSize(const bytevector *v, size_t *index)
{
    size_t value;
    vectorCheckRange(v, *index, sizeof(value));
    value = *(size_t*)&v->data[*index];
    *index += sizeof(value);
    return value;
}


void BVWrite(bytevector *v, size_t *index, byte value)
{
    vectorCheckIndex(v, *index);
    v->data[(*index)++] = value;
}

void BVWriteInt(bytevector *v, size_t *index, int value)
{
    BVSetInt(v, *index, value);
    *index += sizeof(int);
}

void BVWriteUint(bytevector *v, size_t *index, uint value)
{
    BVSetUint(v, *index, value);
    *index += sizeof(int);
}


void BVPopData(bytevector *v, byte *value, size_t size)
{
    vectorCheckRange(v, 0, size);
    v->size -= size;
    memcpy(value, &v->data[v->size], size);
}
