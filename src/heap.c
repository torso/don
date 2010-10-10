#include <memory.h>
#include "common.h"
#include "vm.h"
#include "file.h"
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

static pure boolean isInteger(objectref object)
{
    return (object & INTEGER_LITERAL_MARK) != 0;
}

static objectref boxReference(VM *vm, ObjectType type, ref_t value)
{
    byte *objectData = HeapAlloc(vm, type, sizeof(ref_t));
    if (!objectData)
    {
        return 0;
    }
    *(ref_t*)objectData = value;
    return HeapFinishAlloc(vm, objectData);
}

static ref_t unboxReference(VM *vm, ObjectType type, objectref object)
{
    assert(HeapGetObjectType(vm, object) == type);
    return *(ref_t*)HeapGetObjectData(vm, object);
}

static const char *getString(VM *vm, objectref object)
{
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(vm, object);

    case TYPE_STRING_POOLED:
        return StringPoolGetString(
            unboxReference(vm, TYPE_STRING_POOLED, object));

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
        *(uint32*)(vm->heapBase + sizeFromRef(object) + HEADER_TYPE);
}

size_t HeapGetObjectSize(VM *vm, objectref object)
{
    checkObject(vm, object);
    return *(uint32*)(vm->heapBase + sizeFromRef(object) + HEADER_SIZE);
}

const byte *HeapGetObjectData(VM *vm, objectref object)
{
    checkObject(vm, object);
    return vm->heapBase + sizeFromRef(object) + OBJECT_OVERHEAD;
}


boolean HeapEquals(VM *vm, objectref object1, objectref object2)
{
    Iterator iter1;
    Iterator iter2;
    size_t size1;
    size_t size2;
    boolean success;

    if (object1 == object2)
    {
        return true;
    }
    switch (HeapGetObjectType(vm, object1))
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ITERATOR:
        return false;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        size1 = HeapStringLength(vm, object1);
        size2 = HeapStringLength(vm, object2);
        return size1 == size2 &&
            !memcmp(getString(vm, object1), getString(vm, object2), size1);

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        if (!HeapIsCollection(vm, object2) ||
            HeapCollectionSize(vm, object1) !=
            HeapCollectionSize(vm, object2))
        {
            return false;
        }
        HeapIteratorInit(vm, &iter1, object1, false);
        HeapIteratorInit(vm, &iter2, object2, false);
        while (HeapIteratorNext(&iter1, &object1))
        {
            success = HeapIteratorNext(&iter2, &object2);
            assert(success);
            if (!HeapEquals(vm, object1, object2))
            {
                return false;
            }
        }
        return true;
    }
    assert(false);
    return false;
}

int HeapCompare(VM *vm, objectref object1, objectref object2)
{
    int i1 = HeapUnboxInteger(vm, object1);
    int i2 = HeapUnboxInteger(vm, object2);
    return i1 == i2 ? 0 : i1 < i2 ? -1 : 1;
}


byte *HeapAlloc(VM *vm, ObjectType type, size_t size)
{
    assert(size);
    assert(size <= UINT32_MAX - 1);
    return heapAlloc(vm, type, (uint32)size);
}

objectref HeapFinishAlloc(VM *vm, byte *objectData)
{
    checkObject(vm, (objectref)(objectData - OBJECT_OVERHEAD - vm->heapBase));
    return (objectref)(objectData - OBJECT_OVERHEAD - vm->heapBase);
}


objectref HeapBoxInteger(VM *vm, int value)
{
    assert(value == HeapUnboxInteger(vm, refFromUint((uint)value |
                                                     INTEGER_LITERAL_MARK)));
    return refFromUint((uint)value | INTEGER_LITERAL_MARK);
}

objectref HeapBoxSize(VM *vm, size_t value)
{
    assert(value <= INT_MAX);
    return HeapBoxInteger(vm, (int)value);
}

int HeapUnboxInteger(VM *vm unused, objectref object)
{
    assert(isInteger(object));
    return ((signed)uintFromRef(object) << 1) >> 1;
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
    return boxReference(vm, TYPE_STRING_POOLED, string);
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

size_t HeapStringLength(VM *vm, objectref object)
{
    Iterator iter;
    uint i;
    size_t size;

    if (!object)
    {
        return 4;
    }
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(vm, object);
        size = 1;
        if ((int)i < 0)
        {
            size = 2;
            i = -i;
        }
        while (i > 9)
        {
            i /= 10;
            size++;
        }
        return size;

    case TYPE_STRING:
        return HeapGetObjectSize(vm, object);

    case TYPE_STRING_POOLED:
        return StringPoolGetStringLength(
            unboxReference(vm, TYPE_STRING_POOLED, object));

    case TYPE_FILE:
        return FileGetNameLength(HeapGetFile(vm, object));

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        size = HeapCollectionSize(vm, object);
        if (size)
        {
            size--;
        }
        size = size * 2 + 2;
        HeapIteratorInit(vm, &iter, object, false);
        while (HeapIteratorNext(&iter, &object))
        {
            size += HeapStringLength(vm, object);
        }
        return size;

    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return 0;
}

char *HeapWriteString(VM *vm, objectref object, char *dst)
{
    Iterator iter;
    size_t size;
    fileref file;
    uint i;
    boolean first;

    if (!object)
    {
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return dst;
    }
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_BOOLEAN_TRUE:
        *dst++ = 't';
        *dst++ = 'r';
        *dst++ = 'u';
        *dst++ = 'e';
        return dst;

    case TYPE_BOOLEAN_FALSE:
        *dst++ = 'f';
        *dst++ = 'a';
        *dst++ = 'l';
        *dst++ = 's';
        *dst++ = 'e';
        return dst;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(vm, object);
        if (!i)
        {
            *dst++ = '0';
            return dst;
        }
        size = HeapStringLength(vm, object);
        if ((int)i < 0)
        {
            *dst++ = '-';
            size--;
            i = -i;
        }
        dst += size - 1;
        while (i)
        {
            *dst-- = (char)('0' + i % 10);
            i /= 10;
        }
        return dst + size + 1;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        size = HeapStringLength(vm, object);
        memcpy(dst, getString(vm, object), size);
        return dst + size;

    case TYPE_FILE:
        file = HeapGetFile(vm, object);
        size = FileGetNameLength(file);
        memcpy(dst, FileGetName(file), size);
        return dst + size;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        *dst++ = '[';
        first = true;
        HeapIteratorInit(vm, &iter, object, false);
        while (HeapIteratorNext(&iter, &object))
        {
            if (!first)
            {
                *dst++ = ',';
                *dst++ = ' ';
            }
            first = false;
            dst = HeapWriteString(vm, object, dst);
        }
        *dst++ = ']';
        return dst;

    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return null;
}


objectref HeapCreateFile(VM *vm, fileref file)
{
    return boxReference(vm, TYPE_FILE, file);
}

fileref HeapGetFile(VM *vm, objectref object)
{
    return unboxReference(vm, TYPE_FILE, object);
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


objectref HeapCreateIterator(VM *vm, objectref object)
{
    byte *objectData = HeapAlloc(vm, TYPE_ITERATOR, sizeof(Iterator));
    if (!objectData)
    {
        return 0;
    }
    HeapIteratorInit(vm, (Iterator*)objectData, object, false);
    return HeapFinishAlloc(vm, objectData);
}

void HeapIteratorInit(VM *vm, Iterator *iter, objectref object, boolean flatten)
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
    ref_t *restrict files;
    objectref object;
    size_t count;

    objectData = heapAlloc(vm, TYPE_ARRAY, 0);
    if (!objectData)
    {
        return 0;
    }
    files = (ref_t*)objectData;
    vm->error = FileTraverseGlob(pattern, addFile, vm);
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
        *files = HeapCreateFile(vm, *files);
        if (!*files)
        {
            return 0;
        }
    }
    return object;
}
