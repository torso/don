#include <memory.h>
#include "common.h"
#include "heap.h"
#include "fileindex.h"
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
    case TYPE_FILE:
    case TYPE_ITERATOR:
        return false;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_FILESET:
        return true;
    }
    assert(false);
    return false;
}

static size_t getPageFree(Heap *heap)
{
    return (size_t)(heap->base + PAGE_SIZE - heap->free);
}

static byte *heapAlloc(Heap *heap, ObjectType type, uint32 size)
{
    uint32 *objectData = (uint32*)heap->free;
    assert(OBJECT_OVERHEAD + size <= getPageFree(heap));
    heap->free += OBJECT_OVERHEAD + size;
    *objectData++ = size;
    *objectData++ = type;
    return (byte*)objectData;
}

static uint finishAllocResize(Heap *heap, byte *objectData, uint32 newSize)
{
    checkObject(heap, (uint)(objectData - OBJECT_OVERHEAD - heap->base));
    *(uint32*)(objectData - OBJECT_OVERHEAD + HEADER_SIZE) = newSize;
    return (uint)(objectData - OBJECT_OVERHEAD - heap->base);
}

static void arrayGet(Heap *heap, uint object,
                     ValueType *restrict type, uint *restrict value)
{
    const uint *restrict data = (const uint*)HeapGetObjectData(heap, object);
    *value = data[*value];
    if (!*value)
    {
        *type = TYPE_NULL_LITERAL;
    }
}

static void iterArrayInit(Heap *heap, IteratorState *state,
                          IteratorType type, uint object)
{
    size_t size = HeapCollectionSize(heap, object);

    if (!size)
    {
        state->type = ITER_EMPTY;
        return;
    }
    state->type = type;
    state->current.objectArray =
        (const uint*)HeapGetObjectData(heap, object);
    state->limit.objectArray = state->current.objectArray + size - 1;
}

static void iterArrayNext(IteratorState *state,
                          ValueType *restrict type, uint *restrict value)
{
    *value = *state->current.objectArray;
    if (!*value)
    {
        *type = TYPE_NULL_LITERAL;
    }
    if (state->current.objectArray == state->limit.objectArray)
    {
        state->type = ITER_EMPTY;
    }
    state->current.objectArray++;
}

static void iterStateInit(Heap *heap, IteratorState *state, uint object,
                          boolean flatten)
{
    const int *data;

    state->flatten = flatten;
    switch (HeapGetObjectType(heap, object))
    {
    case TYPE_EMPTY_LIST:
        state->type = ITER_EMPTY;
        return;

    case TYPE_ARRAY:
        iterArrayInit(heap, state, ITER_OBJECT_ARRAY, object);
        return;

    case TYPE_INTEGER_RANGE:
        data = (const int*)HeapGetObjectData(heap, object);
        state->type = ITER_INTEGER_RANGE;
        state->current.value = data[0];
        state->limit.value = data[1];
        return;

    case TYPE_FILESET:
        iterArrayInit(heap, state, ITER_FILESET, object);
        return;

    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return;
    }
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
    return heapAlloc(heap, type, (uint32)size);
}

uint HeapFinishAlloc(Heap *heap, byte *objectData)
{
    checkObject(heap, (uint)(objectData - OBJECT_OVERHEAD - heap->base));
    return (uint)(objectData - OBJECT_OVERHEAD - heap->base);
}

uint HeapAllocString(Heap *heap, const char *restrict string, size_t length)
{
    byte *restrict objectData = HeapAlloc(heap, TYPE_STRING, length);
    if (!objectData)
    {
        return 0;
    }
    memcpy(objectData, string, length);
    return HeapFinishAlloc(heap, objectData);
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
    case TYPE_FILESET:
        return HeapGetObjectSize(heap, object) / sizeof(uint);

    case TYPE_INTEGER_RANGE:
        data = (const int *)HeapGetObjectData(heap, object);
        assert(!subOverflow(data[1], data[0]));
        return (size_t)(data[1] - data[0]) + 1;

    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return 0;
    }
}

boolean HeapCollectionGet(Heap *heap, uint object, ValueType *type,
                          uint *restrict value)
{
    const int *restrict intData;

    assert(*type == TYPE_INTEGER_LITERAL);
    assert((int)*value >= 0);
    assert(*value < HeapCollectionSize(heap, object));
    switch (HeapGetObjectType(heap, object))
    {
    case TYPE_EMPTY_LIST:
        return false;

    case TYPE_ARRAY:
        *type = TYPE_OBJECT;
        arrayGet(heap, object, type, value);
        return true;

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(heap, object);
        assert(!addOverflow((int)*value, intData[0]));
        *value += (uint)intData[0];
        return true;

    case TYPE_FILESET:
        *type = TYPE_FILE_LITERAL;
        arrayGet(heap, object, type, value);
        return true;

    case TYPE_BOOLEAN:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return false;
    }
}

void HeapCollectionIteratorInit(Heap *heap, Iterator *iter, uint object,
                                boolean flatten)
{
    iter->heap = heap;
    iter->state.nextState = &iter->state;
    iterStateInit(heap, &iter->state, object, flatten);
}

boolean HeapIteratorNext(Iterator *iter, ValueType *type, uint *value)
{
    IteratorState *currentState;
    IteratorState *nextState;

    for (;;)
    {
        currentState = iter->state.nextState;
        switch (currentState->type)
        {
        case ITER_EMPTY:
            if (currentState == &iter->state)
            {
                return false;
            }
            nextState = currentState->nextState;
            free(currentState);
            iter->state.nextState = nextState;
            continue;

        case ITER_OBJECT_ARRAY:
            *type = TYPE_OBJECT;
            iterArrayNext(currentState, type, value);
            break;

        case ITER_INTEGER_RANGE:
            *value = (uint)currentState->current.value;
            *type = TYPE_INTEGER_LITERAL;
            if (currentState->current.value == currentState->limit.value)
            {
                currentState->type = ITER_EMPTY;
            }
            currentState->current.value++;
            break;

        case ITER_FILESET:
            *type = TYPE_FILE_LITERAL;
            iterArrayNext(currentState, type, value);
            break;

        default:
            assert(false);
            break;
        }
        if (currentState->flatten && *type == TYPE_OBJECT &&
            HeapIsCollection(iter->heap, *value))
        {
            nextState = (IteratorState*)malloc(sizeof(IteratorState));
            assert(nextState); /* TODO: Error handling. */
            nextState->nextState = currentState;
            iter->state.nextState = nextState;
            iterStateInit(iter->heap, nextState, *value, true);
            continue;
        }
        return true;
    }
}


static ErrorCode addFile(fileref file, void *userdata)
{
    Heap *heap = (Heap*)userdata;
    assert(getPageFree(heap) >= sizeof(fileref)); /* TODO: Error handling. */
    *(fileref*)heap->free = file;
    heap->free += sizeof(fileref);
    return NO_ERROR;
}

ErrorCode HeapCreateFilesetGlob(Heap *heap, const char *pattern,
                                ValueType *restrict type, uint *restrict value)
{
    byte *restrict objectData = heapAlloc(heap, TYPE_FILESET, 0);
    ErrorCode error;

    if (!objectData)
    {
        return OUT_OF_MEMORY;
    }
    error = FileIndexTraverseGlob(pattern, addFile, heap);
    if (error)
    {
        return error;
    }
    *type = TYPE_OBJECT;
    *value = finishAllocResize(heap, objectData,
                               (uint32)(heap->free - objectData));
    return NO_ERROR;
}
