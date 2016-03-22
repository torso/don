#include "config.h"
#include <stdarg.h>
#include "common.h"
#include "heap.h"

#define INITIAL_HEAP_INDEX_SIZE 1
#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define OBJECT_OVERHEAD (sizeof(int) * 2)
#define HEADER_SIZE 0
#define HEADER_TYPE sizeof(int)

static uint HeapPageIndexSize;
static byte **HeapPageIndex;
static byte *HeapPageBase;
static byte *HeapPageFree;
static const byte *HeapPageLimit;
static size_t HeapPageOffset;


static void checkObject(vref object)
{
    assert(object);
}


byte *HeapAlloc(VType type, size_t size)
{
    uint *objectData = (uint*)HeapPageFree;
    assert(size < INT_MAX); /* TODO */
    assert(HeapPageFree + OBJECT_OVERHEAD + size <= HeapPageLimit); /* TODO: Grow heap. */
    HeapPageFree += OBJECT_OVERHEAD + size;
    *objectData++ = (uint)size;
    *objectData++ = type;
    return (byte*)objectData;
}

vref HeapFinishAlloc(byte *objectData)
{
    return refFromSize((size_t)(HeapPageOffset + objectData - OBJECT_OVERHEAD - HeapPageBase));
}

vref HeapFinishRealloc(byte *objectData, size_t size)
{
    assert(size);
    assert(size <= UINT_MAX - 1);
    assert(HeapPageFree == objectData);
    *(uint*)(objectData - OBJECT_OVERHEAD) = (uint)size;
    HeapPageFree += size;
    return HeapFinishAlloc(objectData);
}

void HeapAllocAbort(byte *objectData)
{
    assert(HeapPageFree == objectData);
    HeapPageFree -= OBJECT_OVERHEAD;
}

void HeapFree(vref value)
{
    assert(HeapGetObjectData(value) + HeapGetObjectSize(value) == HeapPageFree);
    HeapPageFree -= OBJECT_OVERHEAD + HeapGetObjectSize(value);
}


vref HeapTop(void)
{
    return refFromSize((size_t)(HeapPageFree - HeapPageBase));
}

vref HeapNext(vref object)
{
    checkObject(object);
    return refFromSize(
        *(uint*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE) +
        sizeFromRef(object) + OBJECT_OVERHEAD);
}


void HeapGet(vref v, HeapObject *ho)
{
    checkObject(v);
    if (VIsInteger(v))
    {
        ho->type = TYPE_INTEGER;
        ho->size = 0;
    }
    else
    {
        byte *p = HeapPageBase + sizeFromRef(v);
        ho->type = (VType)*(uint*)(p + HEADER_TYPE);
        ho->size = (VType)*(uint*)(p + HEADER_SIZE);
        ho->data = p + OBJECT_OVERHEAD;
    }
}

void HeapInit(void)
{
    HeapPageIndex = (byte**)malloc(INITIAL_HEAP_INDEX_SIZE * sizeof(*HeapPageIndex));
    HeapPageIndexSize = INITIAL_HEAP_INDEX_SIZE;
    HeapPageIndex[0] = (byte*)malloc(PAGE_SIZE);
    HeapPageBase = HeapPageIndex[0];
    HeapPageLimit = HeapPageIndex[0] + PAGE_SIZE;
    HeapPageFree = HeapPageIndex[0] + sizeof(int);
    HeapPageOffset = 0;
}

void HeapDispose(void)
{
    byte **p = HeapPageIndex;
    while (HeapPageIndexSize)
    {
        free(*p++);
        HeapPageIndexSize--;
    }
    free(HeapPageIndex);
    HeapPageIndex = null;
}


VType HeapGetObjectType(vref object)
{
    checkObject(object);
    return VIsInteger(object) ? TYPE_INTEGER :
        (VType)*(uint*)(HeapPageBase + sizeFromRef(object) + HEADER_TYPE);
}

size_t HeapGetObjectSize(vref object)
{
    checkObject(object);
    return *(uint*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE);
}

const byte *HeapGetObjectData(vref object)
{
    checkObject(object);
    return HeapPageBase + sizeFromRef(object) + OBJECT_OVERHEAD;
}
