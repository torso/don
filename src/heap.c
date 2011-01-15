#include <memory.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "file.h"
#include "hash.h"
#include "math.h"
#include "stringpool.h"
#include "util.h"

#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define INTEGER_LITERAL_MARK (((objectref)1 << (sizeof(objectref) * 8 - 1)))

#define OBJECT_OVERHEAD 8
#define HEADER_SIZE 0
#define HEADER_TYPE 4

typedef struct
{
    objectref string;
    size_t offset;
    size_t length;
} SubString;

objectref HeapTrue;
objectref HeapFalse;
objectref HeapEmptyString;
objectref HeapEmptyList;
objectref HeapNewline;


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
    const SubString *ss;

    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(vm, object);

    case TYPE_STRING_POOLED:
        return StringPoolGetString(
            unboxReference(vm, TYPE_STRING_POOLED, object));

    case TYPE_STRING_WRAPPED:
        return *(const char**)HeapGetObjectData(vm, object);

    case TYPE_SUBSTRING:
        ss = (const SubString*)HeapGetObjectData(vm, object);
        return &getString(vm, ss->string)[ss->offset];

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return null;
}

static const char *toString(VM *vm, objectref object, boolean *copy)
{
    *copy = false;
    if (HeapIsString(vm, object))
    {
        return getString(vm, object);
    }
    if (HeapIsFile(vm, object))
    {
        return FileGetName(HeapGetFile(vm, object));
    }
    assert(false); /* TODO */
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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
    case TYPE_ITERATOR:
        return false;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
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
    size_t size;

    state->object = object;
    state->flatten = flatten;
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_EMPTY_LIST:
        state->type = ITER_EMPTY;
        return;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        size = HeapCollectionSize(vm, object);
        if (!size)
        {
            state->type = ITER_EMPTY;
            return;
        }
        state->type = ITER_INDEXED;
        state->current.index = 0;
        state->limit.index = size - 1;
        return;

    case TYPE_CONCAT_LIST:
        size = HeapGetObjectSize(vm, object) / sizeof(objectref);
        if (!size)
        {
            state->type = ITER_EMPTY;
            return;
        }
        state->type = ITER_CONCAT_LIST;
        state->current.index = 0;
        state->limit.index = size - 1;
        return;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
    case TYPE_ITERATOR:
    default:
        assert(false);
        return;
    }
}


void HeapInit(VM *vm)
{
    vm->heapBase = (byte*)malloc(PAGE_SIZE);
    vm->heapFree = vm->heapBase + sizeof(int);
    HeapTrue = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_BOOLEAN_TRUE, 0));
    HeapFalse = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_BOOLEAN_FALSE, 0));
    HeapEmptyString = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_STRING, 0));
    HeapEmptyList = HeapFinishAlloc(vm, heapAlloc(vm, TYPE_EMPTY_LIST, 0));
    HeapNewline = HeapCreateString(vm, "\n", 1);
}

void HeapDispose(VM *vm)
{
    free(vm->heapBase);
    vm->heapBase = null;
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

void HeapHash(VM *vm, objectref object, HashState *hash)
{
    Iterator iter;
    fileref file;
    byte value;

    if (!object)
    {
        value = 0;
        HashUpdate(hash, &value, 1);
        return;
    }
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_BOOLEAN_TRUE:
        value = TYPE_BOOLEAN_TRUE;
        HashUpdate(hash, &value, 1);
        break;

    case TYPE_BOOLEAN_FALSE:
        value = TYPE_BOOLEAN_FALSE;
        HashUpdate(hash, &value, 1);
        break;

    case TYPE_INTEGER:
        value = TYPE_INTEGER;
        HashUpdate(hash, &value, 1);
        /* TODO: Make platform independent. */
        HashUpdate(hash, (const byte*)&object, sizeof(object));
        break;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        value = TYPE_STRING;
        HashUpdate(hash, &value, 1);
        HashUpdate(hash, (const byte*)getString(vm, object),
                   HeapStringLength(vm, object));
        break;

    case TYPE_FILE:
        value = TYPE_FILE;
        HashUpdate(hash, &value, 1);
        file = HeapGetFile(vm, object);
        HashUpdate(hash, (const byte*)FileGetName(file),
                   FileGetNameLength(file));
        break;

    case TYPE_EMPTY_LIST:
        value = TYPE_ARRAY;
        HashUpdate(hash, &value, 1);
        break;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        value = TYPE_ARRAY;
        HashUpdate(hash, &value, 1);
        HeapIteratorInit(vm, &iter, object, false);
        while (HeapIteratorNext(&iter, &object))
        {
            /* TODO: Avoid recursion */
            HeapHash(vm, object, hash);
        }
        break;

    case TYPE_ITERATOR:
        assert(false);
        break;
    }
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
    if (!object1 || !object2)
    {
        return false;
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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        size1 = HeapStringLength(vm, object1);
        size2 = HeapStringLength(vm, object2);
        return size1 == size2 &&
            !memcmp(getString(vm, object1), getString(vm, object2), size1);

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
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


boolean HeapIsTrue(VM *vm, objectref object)
{
    if (object == HeapTrue)
    {
        return true;
    }
    if (object == HeapFalse || !object)
    {
        return false;
    }
    if (HeapGetObjectType(vm, object) == TYPE_INTEGER)
    {
        return HeapUnboxInteger(vm, object) != 0;
    }
    if (HeapIsString(vm, object))
    {
        return HeapStringLength(vm, object) != 0;
    }
    if (HeapIsCollection(vm, object))
    {
        return HeapCollectionSize(vm, object) != 0;
    }
    return true;
}


objectref HeapBoxInteger(VM *vm, int value)
{
    assert(value == HeapUnboxInteger(vm, refFromUint((uint)value |
                                                     INTEGER_LITERAL_MARK)));
    return refFromUint((uint)value | INTEGER_LITERAL_MARK);
}

objectref HeapBoxUint(VM *vm, uint value)
{
    assert(value <= INT_MAX);
    return HeapBoxInteger(vm, (int)value);
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

size_t HeapUnboxSize(VM *vm, objectref object)
{
    assert(isInteger(object));
    assert(HeapUnboxInteger(vm, object) >= 0);
    return (size_t)HeapUnboxInteger(vm, object);
}

int HeapIntegerSign(VM *vm, objectref object)
{
    int i = HeapUnboxInteger(vm, object);
    if (i > 0)
    {
        return 1;
    }
    return i >> (sizeof(int) * 8 - 1);
}


objectref HeapCreateString(VM *vm, const char *restrict string, size_t length)
{
    byte *restrict objectData;

    if (!length)
    {
        return HeapEmptyString;
    }

    objectData = HeapAlloc(vm, TYPE_STRING, length);
    memcpy(objectData, string, length);
    return HeapFinishAlloc(vm, objectData);
}

objectref HeapCreateUninitialisedString(VM *vm, size_t length, char **data)
{
    *(byte**)data = HeapAlloc(vm, TYPE_STRING, length);
    return HeapFinishAlloc(vm, (byte*)*data);
}

objectref HeapCreatePooledString(VM *vm, stringref string)
{
   return boxReference(vm, TYPE_STRING_POOLED, string);
}

objectref HeapCreateWrappedString(VM *vm, const char *restrict string,
                                  size_t length)
{
    byte *restrict data;

    if (!length)
    {
        return HeapEmptyString;
    }
    data = HeapAlloc(vm, TYPE_STRING_WRAPPED, sizeof(char*) + sizeof(size_t));
    *(const char**)data = string;
    *(size_t*)&data[sizeof(char*)] = length;
    return HeapFinishAlloc(vm, data);
}

objectref HeapCreateSubstring(VM *vm, objectref string, size_t offset,
                              size_t length)
{
    SubString *ss;
    byte *data;

    assert(HeapIsString(vm, string));
    assert(HeapStringLength(vm, string) >= offset + length);
    if (!length)
    {
        return HeapEmptyString;
    }
    if (length == HeapStringLength(vm, string))
    {
        return string;
    }
    switch (HeapGetObjectType(vm, string))
    {
    case TYPE_STRING:
        break;

    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
        return HeapCreateWrappedString(vm, &getString(vm, string)[offset], length);

    case TYPE_SUBSTRING:
        ss = (SubString*)HeapGetObjectData(vm, string);
        string = ss->string;
        offset += ss->offset;
        break;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
    case TYPE_ITERATOR:
        assert(false);
        break;
    }
    data = HeapAlloc(vm, TYPE_SUBSTRING, sizeof(SubString));
    ss = (SubString*)data;
    ss->string = string;
    ss->offset = offset;
    ss->length = length;
    return HeapFinishAlloc(vm, data);
}

boolean HeapIsString(VM *vm, objectref object)
{
    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        return true;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
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

    case TYPE_STRING_WRAPPED:
        return *(size_t*)&HeapGetObjectData(vm, object)[sizeof(const char**)];

    case TYPE_SUBSTRING:
        return ((const SubString*)HeapGetObjectData(vm, object))->length;

    case TYPE_FILE:
        return FileGetNameLength(HeapGetFile(vm, object));

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
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
    case TYPE_CONCAT_LIST:
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

char *HeapWriteSubstring(VM *vm, objectref object, size_t offset, size_t length,
                         char *dst)
{
    assert(HeapStringLength(vm, object) >= offset + length);
    memcpy(dst, getString(vm, object) + offset, length);
    return dst + length;
}

objectref HeapStringIndexOf(VM *vm, objectref text, size_t startOffset,
                            objectref substring)
{
    size_t textLength = HeapStringLength(vm, text);
    size_t subLength = HeapStringLength(vm, substring);
    const char *pstart = getString(vm, text);
    const char *p = pstart + startOffset;
    const char *plimit = pstart + textLength - subLength + 1;
    const char *s = getString(vm, substring);

    if (!subLength || subLength > textLength)
    {
        return 0;
    }
    while (p < plimit)
    {
        p = (const char*)memchr(p, *s, (size_t)(plimit - p));
        if (!p)
        {
            return 0;
        }
        if (!memcmp(p, s, subLength))
        {
            return HeapBoxSize(vm, (size_t)(p - pstart));
        }
        p++;
    }
    return 0;
}


objectref HeapCreateFile(VM *vm, fileref file)
{
    return boxReference(vm, TYPE_FILE, file);
}

boolean HeapIsFile(VM *vm, objectref object)
{
    return HeapGetObjectType(vm, object) == TYPE_FILE;
}

fileref HeapGetFile(VM *vm, objectref object)
{
    return unboxReference(vm, TYPE_FILE, object);
}

fileref HeapGetAsFile(VM *vm, objectref object)
{
    if (HeapIsString(vm, object))
    {
        return FileAdd(getString(vm, object), HeapStringLength(vm, object));
    }
    return HeapGetFile(vm, object);
}

fileref HeapGetFileFromParts(VM *vm, objectref path, objectref name,
                             objectref extension)
{
    const char *pathString = null;
    const char *nameString;
    const char *extensionString = null;
    size_t pathLength = 0;
    size_t nameLength;
    size_t extensionLength = 0;
    boolean freePath = false;
    boolean freeName;
    boolean freeExtension = false;
    fileref file;

    assert(!path || HeapIsString(vm, path) || HeapIsFile(vm, path));
    assert(HeapIsString(vm, name) || HeapIsFile(vm, name));
    assert(!extension || HeapIsString(vm, extension));

    if (path)
    {
        pathString = toString(vm, path, &freePath);
        pathLength = HeapStringLength(vm, path);
    }
    nameString = toString(vm, name, &freeName);
    nameLength = HeapStringLength(vm, name);
    if (extension)
    {
        extensionString = toString(vm, extension, &freeExtension);
        extensionLength = HeapStringLength(vm, extension);
    }
    file = FileAddRelativeExt(pathString, pathLength,
                              nameString, nameLength,
                              extensionString, extensionLength);
    if (freePath)
    {
        free((void*)pathString);
    }
    if (freeName)
    {
        free((void*)nameString);
    }
    if (freeExtension)
    {
        free((void*)extensionString);
    }
    return file;
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
    p = (int*)objectData;
    p[0] = low;
    p[1] = high;
    return HeapFinishAlloc(vm, objectData);
}

boolean HeapIsRange(VM *vm, objectref object)
{
    return HeapGetObjectType(vm, object) == TYPE_INTEGER_RANGE;
}

objectref HeapRangeLow(VM *vm, objectref range)
{
    return HeapBoxInteger(vm, ((int*)HeapGetObjectData(vm, range))[0]);
}

objectref HeapRangeHigh(VM *vm, objectref range)
{
    return HeapBoxInteger(vm, ((int*)HeapGetObjectData(vm, range))[1]);
}


objectref HeapSplit(VM *vm, objectref string, objectref delimiter,
                    boolean removeEmpty, boolean trimEmptyLastLine)
{
    size_t length;
    size_t delimiterLength;
    size_t offset;
    size_t lastOffset;
    objectref offsetref;
    objectref value;
    intvector substrings;

    assert(HeapIsString(vm, string));
    length = HeapStringLength(vm, string);
    if (!length)
    {
        return HeapEmptyList;
    }
    delimiterLength = HeapStringLength(vm, delimiter);
    if (!delimiterLength || length < delimiterLength)
    {
        return string;
    }
    IntVectorInit(&substrings);
    offset = 0;
    lastOffset = 0;
    for (;;)
    {
        offsetref = HeapStringIndexOf(vm, string, offset, delimiter);
        if (!offsetref)
        {
            if (length != lastOffset || !(removeEmpty || trimEmptyLastLine))
            {
                IntVectorAddRef(&substrings,
                                HeapCreateSubstring(vm, string, lastOffset,
                                                    length - lastOffset));
            }
            break;
        }
        offset = HeapUnboxSize(vm, offsetref);
        if (offset != lastOffset || !removeEmpty)
        {
            IntVectorAddRef(&substrings,
                            HeapCreateSubstring(vm, string, lastOffset,
                                                offset - lastOffset));
        }
        offset += delimiterLength;
        lastOffset = offset;
    }
    value = HeapCreateArrayFromVector(vm, &substrings);
    IntVectorDispose(&substrings);
    return value;
}


objectref HeapCreateArray(VM *vm, const objectref *values, size_t size)
{
    byte *data;
    size *= sizeof(objectref);
    data = HeapAlloc(vm, TYPE_ARRAY, size);
    memcpy(data, values, size);
    return HeapFinishAlloc(vm, data);
}

objectref HeapCreateArrayFromVector(VM *vm, const intvector *values)
{
    return HeapCreateArray(vm, IntVectorGetPointer(values, 0),
                           IntVectorSize(values));
}

objectref HeapConcatList(VM *vm, objectref list1, objectref list2)
{
    byte *data;
    objectref *subLists;

    assert(HeapIsCollection(vm, list1));
    assert(HeapIsCollection(vm, list2));
    if (!HeapCollectionSize(vm, list1))
    {
        return list2;
    }
    if (!HeapCollectionSize(vm, list2))
    {
        return list1;
    }
    data = HeapAlloc(vm, TYPE_CONCAT_LIST, sizeof(objectref) * 2);
    subLists = (objectref*)data;
    subLists[0] = list1;
    subLists[1] = list2;
    return HeapFinishAlloc(vm, data);
}

boolean HeapIsCollection(VM *vm, objectref object)
{
    return isCollectionType(HeapGetObjectType(vm, object));
}

size_t HeapCollectionSize(VM *vm, objectref object)
{
    const byte *data;
    const int *intData;
    const objectref *objects;
    const objectref *limit;
    size_t size;

    switch (HeapGetObjectType(vm, object))
    {
    case TYPE_EMPTY_LIST:
        return 0;

    case TYPE_ARRAY:
        return HeapGetObjectSize(vm, object) / sizeof(objectref);

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(vm, object);
        assert(!subOverflow(intData[1], intData[0]));
        return (size_t)(intData[1] - intData[0]) + 1;

    case TYPE_CONCAT_LIST:
        data = HeapGetObjectData(vm, object);
        objects = (const objectref*)data;
        limit = (const objectref*)(data + HeapGetObjectSize(vm, object));
        size = 0;
        while (objects < limit)
        {
            size += HeapCollectionSize(vm, *objects++);
        }
        return size;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
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
    const objectref *restrict limit;
    const int *restrict intData;
    ssize_t i = HeapUnboxInteger(vm, indexObject);
    size_t index;
    size_t size;

    if (i < 0)
    {
        return false;
    }
    index = (size_t)i;
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
        assert(i <= INT_MAX - 1);
        assert(!addOverflow((int)i, intData[0]));
        *value = HeapBoxInteger(vm, (int)i + intData[0]);
        return true;

    case TYPE_CONCAT_LIST:
        data = (const objectref*)HeapGetObjectData(vm, object);
        limit = data + HeapGetObjectSize(vm, object);
        while (data < limit)
        {
            size = HeapCollectionSize(vm, *data);
            if (index < size)
            {
                assert(index <= INT_MAX);
                return HeapCollectionGet(vm, *data,
                                         HeapBoxSize(vm, index), value);
            }
            index -= size;
            data++;
        }
        return false;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
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
    boolean flatten;

    for (;;)
    {
        flatten = false;
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

        case ITER_INDEXED:
            HeapCollectionGet(iter->vm, currentState->object,
                              HeapBoxSize(iter->vm, currentState->current.index), value);
            if (currentState->current.index == currentState->limit.index)
            {
                currentState->type = ITER_EMPTY;
            }
            currentState->current.index++;
            break;

        case ITER_CONCAT_LIST:
            *value = ((const objectref*)HeapGetObjectData(
                          iter->vm,
                          currentState->object))[currentState->current.index];
            assert(HeapIsCollection(iter->vm, *value));
            if (currentState->current.index == currentState->limit.index)
            {
                currentState->type = ITER_EMPTY;
            }
            currentState->current.index++;
            flatten = true;
            break;

        default:
            assert(false);
            break;
        }
        if (flatten ||
            (currentState->flatten &&
             *value &&
             HeapIsCollection(iter->vm, *value)))
        {
            nextState = (IteratorState*)malloc(sizeof(IteratorState));
            nextState->nextState = currentState;
            iter->state.nextState = nextState;
            iterStateInit(iter->vm, nextState, *value, currentState->flatten);
            continue;
        }
        return true;
    }
}


static void addFile(fileref file, void *userdata)
{
    VM *vm = (VM*)userdata;
    assert(getPageFree(vm) >= sizeof(fileref)); /* TODO: Error handling. */
    *(fileref*)vm->heapFree = file;
    vm->heapFree += sizeof(fileref);
}

objectref HeapCreateFilesetGlob(VM *vm, const char *pattern)
{
    byte *restrict oldFree = vm->heapFree;
    byte *restrict objectData;
    ref_t *restrict files;
    objectref object;
    size_t count;

    objectData = heapAlloc(vm, TYPE_ARRAY, 0);
    files = (ref_t*)objectData;
    FileTraverseGlob(pattern, addFile, vm);
    if (vm->heapFree == objectData)
    {
        vm->heapFree = oldFree;
        return HeapEmptyList;
    }
    object = finishAllocResize(vm, objectData,
                               (uint32)(vm->heapFree - objectData));
    for (count = HeapCollectionSize(vm, object); count; count--, files++)
    {
        *files = HeapCreateFile(vm, *files);
    }
    return object;
}
