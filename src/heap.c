#include <memory.h>
#include "common.h"
#include "vm.h"
#include "fileindex.h"
#include "math.h"
#include "stringpool.h"

#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define INTEGER_LITERAL_MARK (((objectref)1 << (sizeof(objectref) * 8 - 1)))

#define OBJECT_OVERHEAD 8
#define HEADER_SIZE 0
#define HEADER_TYPE 4

static void checkObject(VM *vm, objectref object)
{
    assert(vm);
    assert(object);
}

static pure boolean isInteger(objectref value)
{
    return (value & INTEGER_LITERAL_MARK) != 0;
}

static pure int getInteger(objectref value)
{
    assert(isInteger(value));
    return ((int)value << 1) >> 1;
}

static objectref boxReference(VM *vm, ObjectType type, uint value)
{
    byte *objectData = HeapAlloc(vm, type, sizeof(int));
    if (!objectData)
    {
        return 0;
    }
    *(uint*)objectData = value;
    return HeapFinishAlloc(vm, objectData);
}

static uint unboxReference(VM *vm, ObjectType type, objectref object)
{
    assert(HeapGetObjectType(vm, object) == type);
    return *(uint*)HeapGetObjectData(vm, object);
}


static boolean isCollectionType(ObjectType type)
{
    switch (type)
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_FILE:
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

static size_t getPageFree(VM *vm)
{
    return (size_t)(vm->heapBase + PAGE_SIZE - vm->heapFree);
}

static byte *heapAlloc(VM *vm, ObjectType type, uint32 size)
{
    uint32 *objectData = (uint32*)vm->heapFree;
    assert(OBJECT_OVERHEAD + size <= getPageFree(vm));
    vm->heapFree += OBJECT_OVERHEAD + size;
    *objectData++ = size;
    *objectData++ = type;
    return (byte*)objectData;
}

static objectref finishAllocResize(VM *vm, byte *objectData, uint32 newSize)
{
    checkObject(vm, (objectref)(objectData - OBJECT_OVERHEAD - vm->heapBase));
    *(uint32*)(objectData - OBJECT_OVERHEAD + HEADER_SIZE) = newSize;
    return (objectref)(objectData - OBJECT_OVERHEAD - vm->heapBase);
}

static void iterStateInit(VM *vm, IteratorState *state, objectref object,
                          boolean flatten)
{
    const int *data;
    size_t size;

    state->flatten = flatten;
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_EMPTY_LIST:
        state->type = ITER_EMPTY;
        return;

    case TYPE_ARRAY:
        size = HeapCollectionSize(vm, object);
        if (!size)
        {
            state->type = ITER_EMPTY;
            return;
        }
        state->type = ITER_OBJECT_ARRAY;
        state->current.objectArray =
            (const objectref*)HeapGetObjectData(vm, object);
        state->limit.objectArray = state->current.objectArray + size - 1;
        return;

    case TYPE_INTEGER_RANGE:
        data = (const int*)HeapGetObjectData(vm, object);
        state->type = ITER_INTEGER_RANGE;
        state->current.value = data[0];
        state->limit.value = data[1];
        return;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return;
    }
}


ErrorCode HeapInit(VM *vm)
{
    vm->heapBase = (byte*)malloc(PAGE_SIZE);
    vm->heapFree = vm->heapBase + sizeof(int);
    vm->booleanTrue = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_BOOLEAN_TRUE, 0));
    vm->booleanFalse = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_BOOLEAN_FALSE, 0));
    vm->emptyString = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_STRING, 0));
    vm->emptyList = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_EMPTY_LIST, 0));
    assert(!vm->error);
    return vm->heapBase ? NO_ERROR : OUT_OF_MEMORY;
}

void HeapDispose(VM *vm)
{
    free(vm->heapBase);
}

ObjectType HeapGetObjectType(VM *vm, objectref object)
{
    checkObject(vm, object);
    return isInteger(object) ? TYPE_INTEGER :
        *(uint32*)(vm->heapBase + object + HEADER_TYPE);
}

size_t HeapGetObjectSize(VM *vm, objectref object)
{
    checkObject(vm, object);
    return *(uint32*)(vm->heapBase + object + HEADER_SIZE);
}

const byte *HeapGetObjectData(VM *vm, objectref object)
{
    checkObject(vm, object);
    return vm->heapBase + object + OBJECT_OVERHEAD;
}

byte *HeapAlloc(VM *vm, ObjectType type, size_t size)
{
    assert(size);
    assert(size <= MAX_UINT32 - 1);
    return heapAlloc(vm, type, (uint32)size);
}

objectref HeapFinishAlloc(VM *vm, byte *objectData)
{
    checkObject(vm, (objectref)(objectData - OBJECT_OVERHEAD - vm->heapBase));
    return (objectref)(objectData - OBJECT_OVERHEAD - vm->heapBase);
}


objectref HeapBoxInteger(VM *vm unused, int value)
{
    assert(value == getInteger((objectref)value | INTEGER_LITERAL_MARK));
    return (objectref)value | INTEGER_LITERAL_MARK;
}

objectref HeapBoxSize(VM *vm unused, size_t value)
{
    assert(value <= MAX_INT);
    return HeapBoxInteger(vm, (int)value);
}

int HeapUnboxInteger(VM *vm unused, objectref value)
{
    return getInteger(value);
}


objectref HeapCreateString(VM *vm, const char *restrict string, size_t length)
{
    byte *restrict objectData;

    if (!length)
    {
        return vm->emptyString;
    }

    objectData = HeapAlloc(vm, TYPE_STRING, length);
    if (!objectData)
    {
        return 0;
    }
    memcpy(objectData, string, length);
    return HeapFinishAlloc(vm, objectData);
}

objectref HeapCreatePooledString(VM *vm, stringref string)
{
    return boxReference(vm, TYPE_STRING_POOLED, (uint)string);
}

boolean HeapIsString(VM *vm, objectref object)
{
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        return true;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_ITERATOR:
        return false;
    }
    assert(false);
    return false;
}

const char *HeapGetString(VM *vm, objectref object)
{
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(vm, object);

    case TYPE_STRING_POOLED:
        return StringPoolGetString(
            (stringref)unboxReference(vm, TYPE_STRING_POOLED, object));

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return null;
}

size_t HeapGetStringLength(VM *vm, objectref object)
{
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_STRING:
        return HeapGetObjectSize(vm, object);

    case TYPE_STRING_POOLED:
        return StringPoolGetStringLength(
            (stringref)unboxReference(vm, TYPE_STRING_POOLED, object));

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return null;
}


objectref HeapCreateFile(VM *vm, fileref file)
{
    return boxReference(vm, TYPE_FILE, (uint)file);
}

fileref HeapGetFile(VM *vm, objectref object)
{
    return (fileref)unboxReference(vm, TYPE_FILE, object);
}


objectref HeapCreateRange(VM *vm, objectref lowObject, objectref highObject)
{
    byte *objectData;
    int *p;
    int low = HeapUnboxInteger(vm, lowObject);
    int high = HeapUnboxInteger(vm, highObject);

    assert(low <= high); /* TODO: Reverse range */
    assert(!subOverflow(high, low));
    objectData = HeapAlloc(vm, TYPE_INTEGER_RANGE, 2 * sizeof(int));
    if (!objectData)
    {
        return 0;
    }
    p = (int*)objectData;
    p[0] = low;
    p[1] = high;
    return HeapFinishAlloc(vm, objectData);
}


boolean HeapIsCollection(VM *vm, objectref object)
{
    return isCollectionType(HeapGetObjectType(vm, object));
}

size_t HeapCollectionSize(VM *vm, objectref object)
{
    const int *data;
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_EMPTY_LIST:
        return 0;

    case TYPE_ARRAY:
        return HeapGetObjectSize(vm, object) / sizeof(objectref);

    case TYPE_INTEGER_RANGE:
        data = (const int *)HeapGetObjectData(vm, object);
        assert(!subOverflow(data[1], data[0]));
        return (size_t)(data[1] - data[0]) + 1;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return 0;
    }
}

boolean HeapCollectionGet(VM *vm, objectref object, objectref indexObject,
                          objectref *restrict value)
{
    const objectref *restrict data;
    const int *restrict intData;
    int i = HeapUnboxInteger(vm, indexObject);
    uint index;

    if (i < 0)
    {
        return false;
    }
    index = (uint)i;
    assert(index < HeapCollectionSize(vm, object));
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_EMPTY_LIST:
        return false;

    case TYPE_ARRAY:
        data = (const objectref*)HeapGetObjectData(vm, object);
        *value = data[index];
        return true;

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(vm, object);
        assert(!addOverflow(i, intData[0]));
        *value = HeapBoxInteger(vm, i + intData[0]);
        return true;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return false;
    }
}

void HeapCollectionIteratorInit(VM *vm, Iterator *iter, objectref object,
                                boolean flatten)
{
    iter->vm = vm;
    iter->state.nextState = &iter->state;
    iterStateInit(vm, &iter->state, object, flatten);
}

boolean HeapIteratorNext(Iterator *iter, objectref *value)
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
            *value = *currentState->current.objectArray;
            if (currentState->current.objectArray == currentState->limit.objectArray)
            {
                currentState->type = ITER_EMPTY;
            }
            currentState->current.objectArray++;
            break;

        case ITER_INTEGER_RANGE:
            *value = HeapBoxInteger(iter->vm, currentState->current.value);
            if (currentState->current.value == currentState->limit.value)
            {
                currentState->type = ITER_EMPTY;
            }
            currentState->current.value++;
            break;

        default:
            assert(false);
            break;
        }
        if (currentState->flatten &&
            HeapIsCollection(iter->vm, *value))
        {
            nextState = (IteratorState*)malloc(sizeof(IteratorState));
            assert(nextState); /* TODO: Error handling. */
            nextState->nextState = currentState;
            iter->state.nextState = nextState;
            iterStateInit(iter->vm, nextState, *value, true);
            continue;
        }
        return true;
    }
}


static ErrorCode addFile(fileref file, void *userdata)
{
    VM *vm = (VM*)userdata;
    assert(getPageFree(vm) >= sizeof(fileref)); /* TODO: Error handling. */
    *(fileref*)vm->heapFree = file;
    vm->heapFree += sizeof(fileref);
    return NO_ERROR;
}

objectref HeapCreateFilesetGlob(VM *vm, const char *pattern)
{
    byte *restrict oldFree = vm->heapFree;
    byte *restrict objectData;
    uint *restrict files;
    objectref object;
    size_t count;

    objectData = heapAlloc(vm, TYPE_ARRAY, 0);
    if (!objectData)
    {
        return 0;
    }
    files = (uint*)objectData;
    vm->error = FileIndexTraverseGlob(pattern, addFile, vm);
    if (vm->error)
    {
        return 0;
    }
    if (vm->heapFree == objectData)
    {
        vm->heapFree = oldFree;
        return vm->emptyList;
    }
    object = finishAllocResize(vm, objectData,
                               (uint32)(vm->heapFree - objectData));
    for (count = HeapCollectionSize(vm, object); count; count--, files++)
    {
        *files = (uint)HeapCreateFile(vm, *(fileref*)files);
        if (!*files)
        {
            return 0;
        }
    }
    return object;
}
