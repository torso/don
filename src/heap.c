#include "builder.h"
#include "heap.h"

#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define OBJECT_OVERHEAD 8
#define HEADER_SIZE 0
#define HEADER_TYPE 4

static size_t getPageFree(Heap *heap)
{
    return (size_t)(heap->base + PAGE_SIZE - heap->free);
}

ErrorCode HeapInit(Heap *heap)
{
    heap->base = (byte*)malloc(PAGE_SIZE);
    heap->free = heap->base;
    return heap->base ? NO_ERROR : OUT_OF_MEMORY;
}

void HeapDispose(Heap *heap)
{
    free(heap->base);
}

ObjectType HeapGetObjectType(Heap *heap, uint object)
{
    return *(uint32*)(heap->base + object + HEADER_TYPE);
}

size_t HeapGetObjectSize(Heap *heap, uint object)
{
    return *(uint32*)(heap->base + object + HEADER_SIZE);
}

const byte *HeapGetObjectData(Heap *heap, uint object)
{
    return heap->base + object + OBJECT_OVERHEAD;
}

byte *HeapAlloc(Heap *heap, ObjectType type, size_t size)
{
    uint32 *objectData = (uint32*)heap->free;
    assert(OBJECT_OVERHEAD + size <= getPageFree(heap));
    heap->free += OBJECT_OVERHEAD + size;
    *objectData++ = (uint32)size;
    *objectData++ = type;
    return (byte*)objectData;
}

uint HeapFinishAlloc(Heap *heap, byte *objectData)
{
    return (uint)(objectData - OBJECT_OVERHEAD - heap->base);
}
