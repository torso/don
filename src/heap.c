#include "builder.h"
#include "heap.h"
#include "math.h"

#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define OBJECT_OVERHEAD 8
#define HEADER_SIZE 0
#define HEADER_TYPE 4

static void checkObject(Heap *heap, uint object)
{
    assert(heap);
    assert(object);
}

static boolean isCollectionType(ObjectType type)
{
    switch (type)
    {
    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_ITERATOR:
        return false;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        return true;
    }
    assert(false);
    return false;
}

static size_t getPageFree(Heap *heap)
{
    return (size_t)(heap->base + PAGE_SIZE - heap->free);
}

static byte *heapAlloc(Heap *heap, ObjectType type, size_t size)
{
    uint32 *objectData = (uint32*)heap->free;
    assert(OBJECT_OVERHEAD + size <= getPageFree(heap));
    heap->free += OBJECT_OVERHEAD + size;
    *objectData++ = (uint32)size;
    *objectData++ = type;
    return (byte*)objectData;
}

ErrorCode HeapInit(Heap *heap)
{
    heap->base = (byte*)malloc(PAGE_SIZE);
    heap->free = heap->base + sizeof(uint);
    heap->emptyList = HeapFinishAlloc(heap, heapAlloc(heap, TYPE_EMPTY_LIST, 0));
    return heap->base ? NO_ERROR : OUT_OF_MEMORY;
}

void HeapDispose(Heap *heap)
{
    free(heap->base);
}

ObjectType HeapGetObjectType(Heap *heap, uint object)
{
    checkObject(heap, object);
    return *(uint32*)(heap->base + object + HEADER_TYPE);
}

size_t HeapGetObjectSize(Heap *heap, uint object)
{
    checkObject(heap, object);
    return *(uint32*)(heap->base + object + HEADER_SIZE);
}

const byte *HeapGetObjectData(Heap *heap, uint object)
{
    checkObject(heap, object);
    return heap->base + object + OBJECT_OVERHEAD;
}

byte *HeapAlloc(Heap *heap, ObjectType type, size_t size)
{
    assert(size);
    return heapAlloc(heap, type, size);
}

uint HeapFinishAlloc(Heap *heap, byte *objectData)
{
    checkObject(heap, (uint)(objectData - OBJECT_OVERHEAD - heap->base));
    return (uint)(objectData - OBJECT_OVERHEAD - heap->base);
}

boolean HeapIsCollection(Heap *heap, uint object)
{
    return isCollectionType(HeapGetObjectType(heap, object));
}

size_t HeapCollectionSize(Heap *heap, uint object)
{
    const int *data;
    switch (HeapGetObjectType(heap, object))
    {
    case TYPE_EMPTY_LIST:
        return 0;

    case TYPE_ARRAY:
        return HeapGetObjectSize(heap, object) / sizeof(uint);

    case TYPE_INTEGER_RANGE:
        data = (const int *)HeapGetObjectData(heap, object);
        assert(!subOverflow(data[1], data[0]));
        return (size_t)(data[1] - data[0]) + 1;

    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return 0;
    }
}

void HeapCollectionIteratorInit(Heap *heap, Iterator *iter, uint object)
{
    const int *data;
    iter->heap = heap;
    switch (HeapGetObjectType(heap, object))
    {
    case TYPE_EMPTY_LIST:
        iter->type = ITER_EMPTY;
        return;

    case TYPE_ARRAY:
        iter->type = ITER_OBJECT_ARRAY;
        iter->current.objectArray =
            (const uint*)HeapGetObjectData(heap, object);
        iter->limit.objectArray = iter->current.objectArray +
            HeapCollectionSize(heap, object) - 1;
        return;

    case TYPE_INTEGER_RANGE:
        data = (const int*)HeapGetObjectData(heap, object);
        iter->type = ITER_INTEGER_RANGE;
        iter->current.value = data[0];
        iter->limit.value = data[1];
        return;

    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return;
    }
}

boolean HeapIteratorNext(Iterator *iter, ValueType *type, uint *value)
{
    switch (iter->type)
    {
    case ITER_EMPTY:
        return false;

    case ITER_OBJECT_ARRAY:
        *value = *iter->current.objectArray;
        *type = !*value ? TYPE_NULL_LITERAL : TYPE_OBJECT;
        if (iter->current.objectArray == iter->limit.objectArray)
        {
            iter->type = ITER_EMPTY;
        }
        iter->current.objectArray++;
        return true;

    case ITER_INTEGER_RANGE:
        *value = (uint)iter->current.value;
        *type = TYPE_INTEGER_LITERAL;
        if (iter->current.value == iter->limit.value)
        {
            iter->type = ITER_EMPTY;
        }
        iter->current.value++;
        return true;
    }
    assert(false);
    return false;
}
