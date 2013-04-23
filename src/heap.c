#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "file.h"
#include "hash.h"
#include "math.h"
#include "stringpool.h"
#include "util.h"
#include "work.h"

#define INITIAL_HEAP_INDEX_SIZE 1
#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define INTEGER_LITERAL_MARK (((uint)1 << (sizeof(objectref) * 8 - 1)))
#define INTEGER_LITERAL_MASK (~INTEGER_LITERAL_MARK)
#define INTEGER_LITERAL_SHIFT 1

#define OBJECT_OVERHEAD 8
#define HEADER_SIZE 0
#define HEADER_TYPE 4

typedef struct
{
    objectref string;
    size_t offset;
    size_t length;
} SubString;

static uint HeapPageIndexSize;
static byte **HeapPageIndex;
static byte *HeapPageBase;
static byte *HeapPageFree;
static const byte *HeapPageLimit;
static size_t HeapPageOffset;

objectref HeapTrue;
objectref HeapFalse;
objectref HeapEmptyString;
objectref HeapEmptyList;
objectref HeapNewline;


static void checkObject(objectref object)
{
    assert(object);
}

static pureconst boolean isInteger(objectref object)
{
    return (uintFromRef(object) & INTEGER_LITERAL_MARK) != 0;
}

static objectref boxReference(ObjectType type, ref_t value)
{
    byte *objectData = HeapAlloc(type, sizeof(ref_t));
    *(ref_t*)objectData = value;
    return HeapFinishAlloc(objectData);
}

static ref_t unboxReference(ObjectType type, objectref object)
{
    assert(HeapGetObjectType(object) == type);
    return *(ref_t*)HeapGetObjectData(object);
}

static const char *getString(objectref object)
{
    const SubString *ss;

    assert(!HeapIsFutureValue(object));

    switch (HeapGetObjectType(object))
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(object);

    case TYPE_STRING_POOLED:
        return StringPoolGetString(
            unboxReference(TYPE_STRING_POOLED, object));

    case TYPE_STRING_WRAPPED:
        return *(const char**)HeapGetObjectData(object);

    case TYPE_SUBSTRING:
        ss = (const SubString*)HeapGetObjectData(object);
        return &getString(ss->string)[ss->offset];

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return null;
}

static const char *toString(objectref object, boolean *copy)
{
    *copy = false;
    if (HeapIsString(object))
    {
        return getString(object);
    }
    if (HeapIsFile(object))
    {
        return getString(unboxReference(TYPE_FILE, object));
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
        return false;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return true;

    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return false;
}

static byte *heapAlloc(ObjectType type, uint32 size)
{
    uint32 *objectData = (uint32*)HeapPageFree;
    assert(HeapPageFree + OBJECT_OVERHEAD + size <= HeapPageLimit); /* TODO: Grow heap. */
    HeapPageFree += OBJECT_OVERHEAD + size;
    *objectData++ = size;
    *objectData++ = type;
    return (byte*)objectData;
}

static objectref heapTop(void)
{
    return refFromSize((size_t)(HeapPageFree - HeapPageBase));
}

static objectref heapNext(objectref object)
{
    checkObject(object);
    return refFromSize(
        *(uint32*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE) +
        sizeFromRef(object) + OBJECT_OVERHEAD);
}


void HeapInit(void)
{
    HeapPageIndex = (byte**)calloc(INITIAL_HEAP_INDEX_SIZE, sizeof(*HeapPageIndex));
    HeapPageIndexSize = INITIAL_HEAP_INDEX_SIZE;
    HeapPageIndex[0] = (byte*)malloc(PAGE_SIZE);
    HeapPageBase = HeapPageIndex[0];
    HeapPageLimit = HeapPageIndex[0] + PAGE_SIZE;
    HeapPageFree = HeapPageIndex[0] + sizeof(int);
    HeapPageOffset = 0;
    HeapTrue = HeapFinishAlloc(heapAlloc(TYPE_BOOLEAN_TRUE, 0));
    HeapFalse = HeapFinishAlloc(heapAlloc(TYPE_BOOLEAN_FALSE, 0));
    HeapEmptyString = HeapFinishAlloc(heapAlloc(TYPE_STRING, 0));
    HeapEmptyList = HeapFinishAlloc(heapAlloc(TYPE_EMPTY_LIST, 0));
    HeapNewline = HeapCreateString("\n", 1);
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


char *HeapDebug(objectref object, boolean address)
{
    size_t length = HeapStringLength(object);
    char *buffer = (char*)malloc(length + 16); /* 16 ought to be enough */
    char *p;
    if (address)
    {
        snprintf(buffer, 12, "%u:", object);
        p = buffer + strlen(buffer);
    }
    else
    {
        p = buffer;
    }
    if (!object)
    {
        *p++ = 'n';
        *p++ = 'u';
        *p++ = 'l';
        *p++ = 'l';
    }
    else
    {
        if (HeapIsString(object))
        {
            *p++ = '\"';
        }
        else if (HeapIsFile(object))
        {
            *p++ = '@';
            *p++ = '\"';
        }
        p = HeapWriteString(object, p);
        if (HeapIsString(object) || HeapIsFile(object))
        {
            *p++ = '\"';
        }
    }
    *p++ = 0;
    return buffer;
}

ObjectType HeapGetObjectType(objectref object)
{
    checkObject(object);
    return isInteger(object) ? TYPE_INTEGER :
        (ObjectType)*(uint32*)(HeapPageBase + sizeFromRef(object) + HEADER_TYPE);
}

size_t HeapGetObjectSize(objectref object)
{
    checkObject(object);
    return *(uint32*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE);
}

const byte *HeapGetObjectData(objectref object)
{
    checkObject(object);
    return HeapPageBase + sizeFromRef(object) + OBJECT_OVERHEAD;
}

void HeapHash(VM *vm, objectref object, HashState *hash)
{
    byte value;
    const char *path;
    size_t pathLength;
    size_t index;
    objectref item;

    assert(!HeapIsFutureValue(object));

    if (!object)
    {
        value = 0;
        HashUpdate(hash, &value, 1);
        return;
    }
    switch (HeapGetObjectType(object))
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
        HashUpdate(hash, (const byte*)getString(object),
                   HeapStringLength(object));
        break;

    case TYPE_FILE:
        value = TYPE_FILE;
        HashUpdate(hash, &value, 1);
        path = HeapGetPath(object, &pathLength);
        HashUpdate(hash, (const byte*)path, pathLength);
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
        for (index = 0; HeapCollectionGet(object, HeapBoxSize(index++), &item);)
        {
            /* TODO: Avoid recursion */
            HeapHash(vm, item, hash);
        }
        break;

    case TYPE_FUTURE:
        assert(false);
        break;
    }
}

boolean HeapEquals(objectref object1, objectref object2)
{
    size_t index;
    size_t size1;
    size_t size2;
    objectref item1;
    objectref item2;
    boolean success;

    assert(!HeapIsFutureValue(object1));
    assert(!HeapIsFutureValue(object2));

    if (object1 == object2)
    {
        return true;
    }
    if (!object1 || !object2)
    {
        return false;
    }
    switch (HeapGetObjectType(object1))
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
        return false;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        size1 = HeapStringLength(object1);
        size2 = HeapStringLength(object2);
        return size1 == size2 &&
            !memcmp(getString(object1), getString(object2), size1);

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        if (!HeapIsCollection(object2))
        {
            return false;
        }
        size1 = HeapCollectionSize(object1);
        size2 = HeapCollectionSize(object2);
        if (size1 != size2)
        {
            return false;
        }
        for (index = 0; index < size1; index++)
        {
            success =
                HeapCollectionGet(object1, HeapBoxSize(index), &item1) &&
                HeapCollectionGet(object2, HeapBoxSize(index), &item2);
            assert(success);
            if (!HeapEquals(item1, item2))
            {
                return false;
            }
        }
        return true;

    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return false;
}

int HeapCompare(objectref object1, objectref object2)
{
    int i1 = HeapUnboxInteger(object1);
    int i2 = HeapUnboxInteger(object2);
    return i1 == i2 ? 0 : i1 < i2 ? -1 : 1;
}


byte *HeapAlloc(ObjectType type, size_t size)
{
    assert(size);
    assert(size <= UINT32_MAX - 1);
    return heapAlloc(type, (uint32)size);
}

static objectref heapFinishAlloc(byte *objectData)
{
    return refFromSize((size_t)(HeapPageOffset + objectData - OBJECT_OVERHEAD - HeapPageBase));
}

objectref HeapFinishAlloc(byte *objectData)
{
    objectref object = heapFinishAlloc(objectData);
    return object;
}


boolean HeapIsTrue(objectref object)
{
    assert(!HeapIsFutureValue(object));
    if (object == HeapTrue)
    {
        return true;
    }
    if (object == HeapFalse || !object)
    {
        return false;
    }
    if (HeapGetObjectType(object) == TYPE_INTEGER)
    {
        return HeapUnboxInteger(object) != 0;
    }
    if (HeapIsString(object))
    {
        return HeapStringLength(object) != 0;
    }
    if (HeapIsCollection(object))
    {
        return HeapCollectionSize(object) != 0;
    }
    return true;
}


objectref HeapBoxInteger(int value)
{
    assert(value == HeapUnboxInteger(
               refFromUint(((uint)value & INTEGER_LITERAL_MASK) |
                           INTEGER_LITERAL_MARK)));
    return refFromUint(((uint)value & INTEGER_LITERAL_MASK) |
                       INTEGER_LITERAL_MARK);
}

objectref HeapBoxUint(uint value)
{
    assert(value <= INT_MAX);
    return HeapBoxInteger((int)value);
}

objectref HeapBoxSize(size_t value)
{
    assert(value <= INT_MAX);
    return HeapBoxInteger((int)value);
}

int HeapUnboxInteger(objectref object)
{
    assert(isInteger(object));
    return ((signed)uintFromRef(object) << INTEGER_LITERAL_SHIFT) >>
        INTEGER_LITERAL_SHIFT;
}

size_t HeapUnboxSize(objectref object)
{
    assert(isInteger(object));
    assert(HeapUnboxInteger(object) >= 0);
    return (size_t)HeapUnboxInteger(object);
}

int HeapIntegerSign(objectref object)
{
    int i = HeapUnboxInteger(object);
    if (i > 0)
    {
        return 1;
    }
    return i >> (sizeof(int) * 8 - 1);
}


objectref HeapCreateString(const char *restrict string, size_t length)
{
    byte *restrict objectData;

    if (!length)
    {
        return HeapEmptyString;
    }

    objectData = HeapAlloc(TYPE_STRING, length);
    memcpy(objectData, string, length);
    return HeapFinishAlloc(objectData);
}

objectref HeapCreateUninitialisedString(size_t length, char **data)
{
    assert(length);
    *(byte**)data = HeapAlloc(TYPE_STRING, length);
    return HeapFinishAlloc((byte*)*data);
}

objectref HeapCreatePooledString(stringref string)
{
    return boxReference(TYPE_STRING_POOLED, string);
}

objectref HeapCreateWrappedString(const char *restrict string,
                                  size_t length)
{
    byte *restrict data;

    if (!length)
    {
        return HeapEmptyString;
    }
    data = HeapAlloc(TYPE_STRING_WRAPPED, sizeof(char*) + sizeof(size_t));
    *(const char**)data = string;
    *(size_t*)&data[sizeof(char*)] = length;
    return HeapFinishAlloc(data);
}

objectref HeapCreateSubstring(objectref string, size_t offset, size_t length)
{
    SubString *ss;
    byte *data;

    assert(!HeapIsFutureValue(string));
    assert(HeapIsString(string));
    assert(HeapStringLength(string) >= offset + length);
    if (!length)
    {
        return HeapEmptyString;
    }
    if (length == HeapStringLength(string))
    {
        return string;
    }
    switch (HeapGetObjectType(string))
    {
    case TYPE_STRING:
        break;

    case TYPE_STRING_POOLED:
    case TYPE_STRING_WRAPPED:
        return HeapCreateWrappedString(&getString(string)[offset], length);

    case TYPE_SUBSTRING:
        ss = (SubString*)HeapGetObjectData(string);
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
    case TYPE_FUTURE:
        assert(false);
        break;
    }
    data = HeapAlloc(TYPE_SUBSTRING, sizeof(SubString));
    ss = (SubString*)data;
    ss->string = string;
    ss->offset = offset;
    ss->length = length;
    return HeapFinishAlloc(data);
}

boolean HeapIsString(objectref object)
{
    assert(!HeapIsFutureValue(object));
    switch (HeapGetObjectType(object))
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
        return false;

    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return false;
}

size_t HeapStringLength(objectref object)
{
    uint i;
    size_t size;
    size_t index;
    objectref item;

    assert(!HeapIsFutureValue(object));
    if (!object)
    {
        return 4;
    }
    switch (HeapGetObjectType(object))
    {
    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(object);
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
        return HeapGetObjectSize(object);

    case TYPE_STRING_POOLED:
        return StringPoolGetStringLength(
            unboxReference(TYPE_STRING_POOLED, object));

    case TYPE_STRING_WRAPPED:
        return *(size_t*)&HeapGetObjectData(object)[sizeof(const char**)];

    case TYPE_SUBSTRING:
        return ((const SubString*)HeapGetObjectData(object))->length;

    case TYPE_FILE:
        return HeapStringLength(unboxReference(TYPE_FILE, object));

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        size = HeapCollectionSize(object);
        if (size)
        {
            size--;
        }
        size = size * 2 + 2;
        for (index = 0; HeapCollectionGet(object, HeapBoxSize(index++), &item);)
        {
            size += HeapStringLength(item);
        }
        return size;

    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return 0;
}

char *HeapWriteString(objectref object, char *dst)
{
    size_t size;
    uint i;
    size_t index;
    objectref item;

    assert(!HeapIsFutureValue(object));
    if (!object)
    {
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return dst;
    }
    switch (HeapGetObjectType(object))
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
        i = (uint)HeapUnboxInteger(object);
        if (!i)
        {
            *dst++ = '0';
            return dst;
        }
        size = HeapStringLength(object);
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
        size = HeapStringLength(object);
        memcpy(dst, getString(object), size);
        return dst + size;

    case TYPE_FILE:
        return HeapWriteString(unboxReference(TYPE_FILE, object), dst);

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        *dst++ = '[';
        for (index = 0; HeapCollectionGet(object, HeapBoxSize(index), &item);
             index++)
        {
            if (index)
            {
                *dst++ = ',';
                *dst++ = ' ';
            }
            dst = HeapWriteString(item, dst);
        }
        *dst++ = ']';
        return dst;

    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return null;
}

char *HeapWriteSubstring(objectref object, size_t offset, size_t length,
                         char *dst)
{
    assert(HeapStringLength(object) >= offset + length);
    memcpy(dst, getString(object) + offset, length);
    return dst + length;
}

objectref HeapStringIndexOf(objectref text, size_t startOffset,
                            objectref substring)
{
    size_t textLength = HeapStringLength(text);
    size_t subLength = HeapStringLength(substring);
    const char *pstart = getString(text);
    const char *p = pstart + startOffset;
    const char *plimit = pstart + textLength - subLength + 1;
    const char *s = getString(substring);

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
            return HeapBoxSize((size_t)(p - pstart));
        }
        p++;
    }
    return 0;
}


objectref HeapCreatePath(objectref path)
{
    const char *src;
    size_t srcLength;
    char *temp;
    size_t tempLength;

    if (HeapIsFile(path))
    {
        return path;
    }
    src = getString(path);
    srcLength = HeapStringLength(path);
    /* TODO: Avoid malloc */
    temp = FileCreatePath(null, 0, src, srcLength, null, 0, &tempLength);
    if (tempLength != srcLength && memcmp(src, temp, srcLength))
    {
        path = HeapCreateString(temp, tempLength);
    }
    free(temp);
    return boxReference(TYPE_FILE, path);
}

const char *HeapGetPath(objectref path, size_t *length)
{
    objectref s = unboxReference(TYPE_FILE, path);
    *length = HeapStringLength(s);
    return getString(s);
}

boolean HeapIsFile(objectref object)
{
    return HeapGetObjectType(object) == TYPE_FILE;
}

objectref HeapPathFromParts(objectref path, objectref name, objectref extension)
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
    char *resultPath;
    size_t resultPathLength;
    objectref result;

    assert(!HeapIsFutureValue(path));
    assert(!HeapIsFutureValue(name));
    assert(!HeapIsFutureValue(extension));
    assert(!path || HeapIsString(path) || HeapIsFile(path));
    assert(HeapIsString(name) || HeapIsFile(name));
    assert(!extension || HeapIsString(extension));

    if (path)
    {
        pathString = toString(path, &freePath);
        pathLength = HeapStringLength(path);
    }
    nameString = toString(name, &freeName);
    nameLength = HeapStringLength(name);
    if (extension)
    {
        extensionString = toString(extension, &freeExtension);
        extensionLength = HeapStringLength(extension);
    }
    resultPath = FileCreatePath(pathString, pathLength,
                                nameString, nameLength,
                                extensionString, extensionLength,
                                &resultPathLength);
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
    result = HeapCreatePath(HeapCreateString(resultPath, resultPathLength));
    free(resultPath);
    return result;
}


static void createPath(const char *path, size_t length, void *userdata)
{
    HeapCreatePath(HeapCreateString(path, length));
    (*(size_t*)userdata)++;
}

objectref HeapCreateFilesetGlob(const char *pattern, size_t length)
{
    objectref object = heapTop();
    size_t count = 0;
    byte *objectData;
    objectref *files;

    FileTraverseGlob(pattern, length, createPath, &count);
    if (!count)
    {
        return HeapEmptyList;
    }
    objectData = HeapAlloc(TYPE_ARRAY, count * sizeof(objectref)); /* TODO: Fileset type */
    files = (objectref*)objectData;
    while (count--)
    {
        assert(HeapIsString(object));
        object = heapNext(object);
        assert(HeapIsFile(object));
        *files++ = object;
        object = heapNext(object);
    }
    /* TODO: Sort fileset */
    return HeapFinishAlloc(objectData);
}


objectref HeapCreateRange(objectref lowObject, objectref highObject)
{
    byte *objectData;
    int *p;
    int low = HeapUnboxInteger(lowObject);
    int high = HeapUnboxInteger(highObject);

    assert(low <= high); /* TODO: Reverse range */
    assert(!subOverflow(high, low));
    objectData = HeapAlloc(TYPE_INTEGER_RANGE, 2 * sizeof(int));
    p = (int*)objectData;
    p[0] = low;
    p[1] = high;
    return HeapFinishAlloc(objectData);
}

boolean HeapIsRange(objectref object)
{
    return HeapGetObjectType(object) == TYPE_INTEGER_RANGE;
}

objectref HeapRangeLow(objectref range)
{
    return HeapBoxInteger(((int*)HeapGetObjectData(range))[0]);
}

objectref HeapRangeHigh(objectref range)
{
    return HeapBoxInteger(((int*)HeapGetObjectData(range))[1]);
}


objectref HeapSplit(objectref string, objectref delimiter, boolean removeEmpty,
                    boolean trimLastIfEmpty)
{
    size_t length;
    size_t delimiterLength;
    size_t offset;
    size_t lastOffset;
    objectref offsetref;
    objectref value;
    intvector substrings;

    assert(HeapIsString(string));
    length = HeapStringLength(string);
    if (!length)
    {
        return HeapEmptyList;
    }
    delimiterLength = HeapStringLength(delimiter);
    if (!delimiterLength || length < delimiterLength)
    {
        return string;
    }
    IVInit(&substrings, 4);
    offset = 0;
    lastOffset = 0;
    for (;;)
    {
        offsetref = HeapStringIndexOf(string, offset, delimiter);
        if (!offsetref)
        {
            if (length != lastOffset || !(removeEmpty || trimLastIfEmpty))
            {
                IVAddRef(&substrings,
                         HeapCreateSubstring(string, lastOffset,
                                             length - lastOffset));
            }
            break;
        }
        offset = HeapUnboxSize(offsetref);
        if (offset != lastOffset || !removeEmpty)
        {
            IVAddRef(&substrings,
                     HeapCreateSubstring(string, lastOffset,
                                         offset - lastOffset));
        }
        offset += delimiterLength;
        lastOffset = offset;
    }
    value = HeapCreateArrayFromVector(&substrings);
    IVDispose(&substrings);
    return value;
}


objectref HeapCreateArray(const objectref *values, size_t size)
{
    byte *data;
    size *= sizeof(objectref);
    data = HeapAlloc(TYPE_ARRAY, size);
    memcpy(data, values, size);
    return HeapFinishAlloc(data);
}

objectref HeapCreateArrayFromVector(const intvector *values)
{
    if (!IVSize(values))
    {
        return HeapEmptyList;
    }
    return HeapCreateArray((const objectref*)IVGetPointer(values, 0),
                           IVSize(values));
}

objectref HeapConcatList(objectref list1, objectref list2)
{
    byte *data;
    objectref *subLists;

    assert(HeapIsCollection(list1));
    assert(HeapIsCollection(list2));
    if (!HeapCollectionSize(list1))
    {
        return list2;
    }
    if (!HeapCollectionSize(list2))
    {
        return list1;
    }
    data = HeapAlloc(TYPE_CONCAT_LIST, sizeof(objectref) * 2);
    subLists = (objectref*)data;
    subLists[0] = list1;
    subLists[1] = list2;
    return HeapFinishAlloc(data);
}

boolean HeapIsCollection(objectref object)
{
    return isCollectionType(HeapGetObjectType(object));
}

size_t HeapCollectionSize(objectref object)
{
    const byte *data;
    const int *intData;
    const objectref *objects;
    const objectref *limit;
    size_t size;

    assert(!HeapIsFutureValue(object));
    switch (HeapGetObjectType(object))
    {
    case TYPE_EMPTY_LIST:
        return 0;

    case TYPE_ARRAY:
        return HeapGetObjectSize(object) / sizeof(objectref);

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(object);
        assert(!subOverflow(intData[1], intData[0]));
        return (size_t)(intData[1] - intData[0]) + 1;

    case TYPE_CONCAT_LIST:
        data = HeapGetObjectData(object);
        objects = (const objectref*)data;
        limit = (const objectref*)(data + HeapGetObjectSize(object));
        size = 0;
        while (objects < limit)
        {
            size += HeapCollectionSize(*objects++);
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
    case TYPE_FUTURE:
    default:
        assert(false);
        return 0;
    }
}

boolean HeapCollectionGet(objectref object, objectref indexObject,
                          objectref *restrict value)
{
    const objectref *restrict data;
    const objectref *restrict limit;
    const int *restrict intData;
    ssize_t i;
    size_t index;
    size_t size;

    assert(!HeapIsFutureValue(object));
    assert(!HeapIsFutureValue(indexObject));

    i = HeapUnboxInteger(indexObject);
    if (i < 0)
    {
        return false;
    }
    index = (size_t)i;
    if (index >= HeapCollectionSize(object))
    {
        return false;
    }
    switch (HeapGetObjectType(object))
    {
    case TYPE_EMPTY_LIST:
        return false;

    case TYPE_ARRAY:
        data = (const objectref*)HeapGetObjectData(object);
        *value = data[index];
        return true;

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(object);
        assert(i <= INT_MAX - 1);
        assert(!addOverflow((int)i, intData[0]));
        *value = HeapBoxInteger((int)i + intData[0]);
        return true;

    case TYPE_CONCAT_LIST:
        data = (const objectref*)HeapGetObjectData(object);
        limit = data + HeapGetObjectSize(object);
        while (data < limit)
        {
            size = HeapCollectionSize(*data);
            if (index < size)
            {
                assert(index <= INT_MAX);
                return HeapCollectionGet(*data, HeapBoxSize(index), value);
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
    case TYPE_FUTURE:
    default:
        assert(false);
        return false;
    }
}


typedef struct
{
    objectref value;
    Instruction op;
} FutureValueUnary;

typedef struct
{
    objectref value1;
    objectref value2;
    Instruction op;
} FutureValueBinary;

static objectref executeUnary(Instruction op, objectref value)
{
    switch (op)
    {
    case OP_DUP:
        return value;

    case OP_CAST_BOOLEAN:
        return HeapIsTrue(value) ? HeapTrue : HeapFalse;

    case OP_NOT:
        assert(value == HeapTrue || value == HeapFalse);
        return value == HeapFalse ? HeapTrue : HeapFalse;

    case OP_NEG:
        assert(HeapUnboxInteger(value) != INT_MIN);
        return HeapBoxInteger(-HeapUnboxInteger(value));
        break;

    case OP_INV:
        return HeapBoxInteger(~HeapUnboxInteger(value));

    case OP_ITER_GET:
    case OP_NULL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EMPTY_LIST:
    case OP_LIST:
    case OP_FILESET:
    case OP_POP:
    case OP_LOAD:
    case OP_STORE:
    case OP_LOAD_FIELD:
    case OP_STORE_FIELD:
    case OP_EQUALS:
    case OP_NOT_EQUALS:
    case OP_LESS_EQUALS:
    case OP_GREATER_EQUALS:
    case OP_LESS:
    case OP_GREATER:
    case OP_AND:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_REM:
    case OP_CONCAT_LIST:
    case OP_CONCAT_STRING:
    case OP_INDEXED_ACCESS:
    case OP_RANGE:
    case OP_JUMP:
    case OP_BRANCH_TRUE:
    case OP_BRANCH_FALSE:
    case OP_RETURN:
    case OP_RETURN_VOID:
    case OP_INVOKE:
    case OP_INVOKE_NATIVE:
    case OP_UNKNOWN_VALUE:
        break;
    }
    assert(false);
    return null;
}

static objectref executeBinaryPartial(Instruction op, objectref object,
                                      objectref value1, objectref value2)
{
    switch (op)
    {
    case OP_DUP:
        return value1;

    case OP_EQUALS:
    case OP_LESS_EQUALS:
    case OP_GREATER_EQUALS:
        if (value1 == value2)
        {
            return HeapTrue;
        }
        return object;
    case OP_NOT_EQUALS:
    case OP_LESS:
    case OP_GREATER:
        if (value1 == value2)
        {
            return HeapFalse;
        }
        return object;

    case OP_AND:
        if ((!HeapIsFutureValue(value1) && !HeapIsTrue(value1)) ||
            (!HeapIsFutureValue(value2) && !HeapIsTrue(value2)))
        {
            return HeapFalse;
        }
        return object;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_REM:
    case OP_CONCAT_LIST:
    case OP_CONCAT_STRING:
    case OP_INDEXED_ACCESS:
    case OP_RANGE:
        return object;

    case OP_NULL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EMPTY_LIST:
    case OP_LIST:
    case OP_FILESET:
    case OP_POP:
    case OP_LOAD:
    case OP_STORE:
    case OP_LOAD_FIELD:
    case OP_STORE_FIELD:
    case OP_CAST_BOOLEAN:
    case OP_NOT:
    case OP_NEG:
    case OP_INV:
    case OP_ITER_GET:
    case OP_JUMP:
    case OP_BRANCH_TRUE:
    case OP_BRANCH_FALSE:
    case OP_RETURN:
    case OP_RETURN_VOID:
    case OP_INVOKE:
    case OP_INVOKE_NATIVE:
    case OP_UNKNOWN_VALUE:
        break;
    }
    assert(false);
    return null;
}

static objectref executeBinary(Instruction op,
                               objectref value1, objectref value2)
{
    byte *data;
    size_t size1;
    size_t size2;

    switch (op)
    {
    case OP_DUP:
        return value1;

    case OP_EQUALS:
        return HeapEquals(value1, value2) ? HeapTrue : HeapFalse;
    case OP_NOT_EQUALS:
        return HeapEquals(value1, value2) ? HeapFalse : HeapTrue;
    case OP_LESS_EQUALS:
        return HeapCompare(value2, value1) <= 0 ? HeapTrue : HeapFalse;
    case OP_GREATER_EQUALS:
        return HeapCompare(value2, value1) >= 0 ? HeapTrue : HeapFalse;
    case OP_LESS:
        return HeapCompare(value2, value1) < 0 ? HeapTrue : HeapFalse;
    case OP_GREATER:
        return HeapCompare(value2, value1) > 0 ? HeapTrue : HeapFalse;

    case OP_AND:
        return HeapIsTrue(value1) && HeapIsTrue(value2) ? HeapTrue : HeapFalse;
    case OP_ADD:
        return HeapBoxInteger(HeapUnboxInteger(value2) +
                              HeapUnboxInteger(value1));
    case OP_SUB:
        return HeapBoxInteger(HeapUnboxInteger(value2) -
                              HeapUnboxInteger(value1));
    case OP_MUL:
        return HeapBoxInteger(HeapUnboxInteger(value2) *
                              HeapUnboxInteger(value1));
    case OP_DIV:
        assert((HeapUnboxInteger(value2) /
                HeapUnboxInteger(value1)) *
               HeapUnboxInteger(value1) ==
               HeapUnboxInteger(value2)); /* TODO: fraction */
        return HeapBoxInteger(HeapUnboxInteger(value2) /
                              HeapUnboxInteger(value1));
    case OP_REM:
        return HeapBoxInteger(HeapUnboxInteger(value2) %
                              HeapUnboxInteger(value1));

    case OP_CONCAT_LIST:
        return HeapConcatList(value2, value1);

    case OP_CONCAT_STRING:
        size1 = HeapStringLength(value2);
        size2 = HeapStringLength(value1);
        if (!size1 && !size2)
        {
            return HeapEmptyString;
        }
        data = HeapAlloc(TYPE_STRING, size1 + size2);
        HeapWriteString(value2, (char*)data);
        HeapWriteString(value1, (char*)data + size1);
        return HeapFinishAlloc(data);

    case OP_INDEXED_ACCESS:
        if (HeapIsString(value2))
        {
            if (HeapIsRange(value1))
            {
                size1 = HeapUnboxSize(HeapRangeLow(value1));
                size2 = HeapUnboxSize(HeapRangeHigh(value1));
                assert(size2 >= size1); /* TODO: Support inverted ranges. */
                value1 = HeapCreateSubstring(value2, size1, size2 - size1 + 1);
            }
            else
            {
                value1 = HeapCreateSubstring(value2, HeapUnboxSize(value1), 1);
            }
        }
        else
        {
            assert(HeapUnboxInteger(value1) >= 0);
            assert((size_t)HeapUnboxInteger(value1) < HeapCollectionSize(value2));
            if (!HeapCollectionGet(value2, value1, &value1))
            {
                assert(false);
                return 0;
            }
        }
        return value1;

    case OP_RANGE:
        return HeapCreateRange(value2, value1);

    case OP_NULL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EMPTY_LIST:
    case OP_LIST:
    case OP_FILESET:
    case OP_POP:
    case OP_LOAD:
    case OP_STORE:
    case OP_LOAD_FIELD:
    case OP_STORE_FIELD:
    case OP_CAST_BOOLEAN:
    case OP_NOT:
    case OP_NEG:
    case OP_INV:
    case OP_ITER_GET:
    case OP_JUMP:
    case OP_BRANCH_TRUE:
    case OP_BRANCH_FALSE:
    case OP_RETURN:
    case OP_RETURN_VOID:
    case OP_INVOKE:
    case OP_INVOKE_NATIVE:
    case OP_UNKNOWN_VALUE:
        break;
    }
    assert(false);
    return null;
}

boolean HeapIsFutureValue(objectref object)
{
    return object && HeapGetObjectType(object) == TYPE_FUTURE;
}

objectref HeapCreateFutureValue(void)
{
    byte *data = HeapAlloc(TYPE_FUTURE, sizeof(FutureValueUnary));
    FutureValueUnary *future = (FutureValueUnary*)data;
    future->value = 0;
    future->op = OP_UNKNOWN_VALUE;
    return HeapFinishAlloc(data);
}

void HeapSetFutureValue(objectref object, objectref value)
{
    FutureValueUnary *future = (FutureValueUnary*)HeapGetObjectData(object);
    assert(HeapGetObjectSize(object) == sizeof(FutureValueUnary));
    assert(!future->value);
    assert(future->op == OP_UNKNOWN_VALUE);
    future->value = value;
    future->op = OP_DUP;
}

objectref HeapTryWait(VM *vm, objectref object)
{
    FutureValueUnary *future1;
    FutureValueBinary *future2;

    if (!HeapIsFutureValue(object))
    {
        return object;
    }
    if (HeapGetObjectSize(object) == sizeof(FutureValueUnary))
    {
        future1 = (FutureValueUnary*)HeapGetObjectData(object);
        if (future1->op == OP_UNKNOWN_VALUE)
        {
            return object;
        }
        future1->value = HeapTryWait(vm, future1->value);
        return HeapIsFutureValue(future1->value) ?
            object : executeUnary(future1->op, future1->value);
    }
    else
    {
        future2 = (FutureValueBinary*)HeapGetObjectData(object);
        if (future2->op == OP_UNKNOWN_VALUE)
        {
            return object;
        }
        future2->value1 = HeapTryWait(vm, future2->value1);
        future2->value2 = HeapTryWait(vm, future2->value2);
        return (HeapIsFutureValue(future2->value1) ||
                HeapIsFutureValue(future2->value2)) ?
            executeBinaryPartial(future2->op, object,
                                 future2->value1, future2->value2) :
            executeBinary(future2->op, future2->value1, future2->value2);
    }
}

objectref HeapWait(VM *vm, objectref object)
{
    object = HeapTryWait(vm, object);
    while (HeapIsFutureValue(object))
    {
        WorkExecute();
        object = HeapTryWait(vm, object);
    }
    return object;
}


objectref HeapApplyUnary(VM *vm, Instruction op, objectref value)
{
    byte *data;
    FutureValueUnary *future;

    value = HeapTryWait(vm, value);
    if (HeapIsFutureValue(value))
    {
        data = HeapAlloc(TYPE_FUTURE, sizeof(FutureValueUnary));
        future = (FutureValueUnary*)data;
        future->value = value;
        future->op = op;
        return HeapFinishAlloc(data);
    }
    return executeUnary(op, value);
}

objectref HeapApplyBinary(VM *vm, Instruction op,
                          objectref value1, objectref value2)
{
    byte *data;
    FutureValueBinary *future;

    value1 = HeapTryWait(vm, value1);
    value2 = HeapTryWait(vm, value2);
    if (HeapIsFutureValue(value1) || HeapIsFutureValue(value2))
    {
        data = HeapAlloc(TYPE_FUTURE, sizeof(FutureValueBinary));
        future = (FutureValueBinary*)data;
        future->value1 = value1;
        future->value2 = value2;
        future->op = op;
        return HeapFinishAlloc(data);
    }
    return executeBinary(op, value1, value2);
}
