#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "fail.h"
#include "file.h"
#include "hash.h"
#include "math.h"
#include "parser.h"
#include "stringpool.h"
#include "util.h"
#include "work.h"

#define INITIAL_HEAP_INDEX_SIZE 1
#define PAGE_SIZE ((size_t)(1024 * 1024 * 1024))

#define INTEGER_LITERAL_MARK (((uint)1 << (sizeof(vref) * 8 - 1)))
#define INTEGER_LITERAL_MASK (~INTEGER_LITERAL_MARK)
#define INTEGER_LITERAL_SHIFT 1

#define OBJECT_OVERHEAD (sizeof(int) * 2)
#define HEADER_SIZE 0
#define HEADER_TYPE sizeof(int)

static uint HeapPageIndexSize;
static byte **HeapPageIndex;
static byte *HeapPageBase;
static byte *HeapPageFree;
static const byte *HeapPageLimit;
static size_t HeapPageOffset;

vref HeapTrue;
vref HeapFalse;
vref HeapEmptyString;
vref HeapEmptyList;
vref HeapNewline;
vref HeapInvalid;


static void checkObject(vref object)
{
    assert(object);
}

static pureconst boolean isInteger(vref object)
{
    return (uintFromRef(object) & INTEGER_LITERAL_MARK) != 0;
}

static vref boxReference(VType type, ref_t value)
{
    byte *objectData = HeapAlloc(type, sizeof(ref_t));
    *(ref_t*)objectData = value;
    return HeapFinishAlloc(objectData);
}

static ref_t unboxReference(VType type, vref object)
{
    assert(HeapGetObjectType(object) == type);
    return *(ref_t*)HeapGetObjectData(object);
}

static const char *getString(vref object)
{
    const SubString *ss;

    assert(!HeapIsFutureValue(object));

    switch (HeapGetObjectType(object))
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(object);

    case TYPE_STRING_WRAPPED:
        return *(const char**)HeapGetObjectData(object);

    case TYPE_SUBSTRING:
        ss = (const SubString*)HeapGetObjectData(object);
        return &getString(ss->string)[ss->offset];

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
    case TYPE_FUTURE:
    case TYPE_INVALID:
        break;
    }
    assert(false);
    return null;
}

static const char *toString(vref object, boolean *copy)
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


static boolean isCollectionType(VType type)
{
    switch (type)
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
        return false;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return true;

    case TYPE_FUTURE:
    case TYPE_INVALID:
        break;
    }
    assert(false);
    return false;
}

void HeapGet(vref v, HeapObject *ho)
{
    checkObject(v);
    if (isInteger(v))
    {
        ho->type = TYPE_INTEGER;
        ho->size = 0;
    }
    else
    {
        byte *p = HeapPageBase + sizeFromRef(v);
        ho->type = (VType)*(uint32*)(p + HEADER_TYPE);
        ho->size = (VType)*(uint32*)(p + HEADER_SIZE);
        ho->data = p + OBJECT_OVERHEAD;
    }
}

static byte *heapAlloc(VType type, uint32 size)
{
    uint32 *objectData = (uint32*)HeapPageFree;
    assert(HeapPageFree + OBJECT_OVERHEAD + size <= HeapPageLimit); /* TODO: Grow heap. */
    HeapPageFree += OBJECT_OVERHEAD + size;
    *objectData++ = size;
    *objectData++ = type;
    return (byte*)objectData;
}

static vref heapFinishAlloc(byte *objectData)
{
    return refFromSize((size_t)(HeapPageOffset + objectData - OBJECT_OVERHEAD - HeapPageBase));
}

static vref heapFinishRealloc(byte *objectData, size_t size)
{
    assert(size);
    assert(size <= UINT32_MAX - 1);
    assert(HeapPageFree == objectData);
    *(uint32*)(objectData - OBJECT_OVERHEAD) = (uint32)size;
    HeapPageFree += size;
    return heapFinishAlloc(objectData);
}

static void heapAllocAbort(byte *objectData)
{
    assert(HeapPageFree == objectData);
    HeapPageFree -= OBJECT_OVERHEAD;
}

static void heapFree(vref value)
{
    assert(HeapGetObjectData(value) + HeapGetObjectSize(value) == HeapPageFree);
    HeapPageFree -= OBJECT_OVERHEAD + HeapGetObjectSize(value);
}


static vref heapTop(void)
{
    return refFromSize((size_t)(HeapPageFree - HeapPageBase));
}

static vref heapNext(vref object)
{
    checkObject(object);
    return refFromSize(
        *(uint32*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE) +
        sizeFromRef(object) + OBJECT_OVERHEAD);
}


void HeapInit(void)
{
    byte *p;

    HeapPageIndex = (byte**)malloc(INITIAL_HEAP_INDEX_SIZE * sizeof(*HeapPageIndex));
    HeapPageIndexSize = INITIAL_HEAP_INDEX_SIZE;
    HeapPageIndex[0] = (byte*)malloc(PAGE_SIZE);
    HeapPageBase = HeapPageIndex[0];
    HeapPageLimit = HeapPageIndex[0] + PAGE_SIZE;
    HeapPageFree = HeapPageIndex[0] + sizeof(int);
    HeapPageOffset = 0;
    StringPoolInit();
    ParserAddKeywords();
    HeapTrue = HeapFinishAlloc(heapAlloc(TYPE_BOOLEAN_TRUE, 0));
    HeapFalse = HeapFinishAlloc(heapAlloc(TYPE_BOOLEAN_FALSE, 0));
    p = heapAlloc(TYPE_STRING, 1);
    *p = 0;
    HeapEmptyString = HeapFinishAlloc(p);
    HeapEmptyList = HeapFinishAlloc(heapAlloc(TYPE_ARRAY, 0));
    HeapNewline = HeapCreateString("\n", 1);
    HeapInvalid = HeapFinishAlloc(heapAlloc(TYPE_INVALID, 0));
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


char *HeapDebug(vref object, boolean address)
{
    size_t length = VStringLength(object);
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
        p = VWriteString(object, p);
        if (HeapIsString(object) || HeapIsFile(object))
        {
            *p++ = '\"';
        }
    }
    *p++ = 0;
    return buffer;
}

VType HeapGetObjectType(vref object)
{
    checkObject(object);
    return isInteger(object) ? TYPE_INTEGER :
        (VType)*(uint32*)(HeapPageBase + sizeFromRef(object) + HEADER_TYPE);
}

size_t HeapGetObjectSize(vref object)
{
    checkObject(object);
    return *(uint32*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE);
}

const byte *HeapGetObjectData(vref object)
{
    checkObject(object);
    return HeapPageBase + sizeFromRef(object) + OBJECT_OVERHEAD;
}

void HeapHash(vref object, HashState *hash)
{
    byte value;
    const char *path;
    size_t pathLength;
    size_t index;
    vref item;

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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        value = TYPE_STRING;
        HashUpdate(hash, &value, 1);
        HashUpdate(hash, (const byte*)getString(object),
                   VStringLength(object));
        break;

    case TYPE_FILE:
        value = TYPE_FILE;
        HashUpdate(hash, &value, 1);
        path = HeapGetPath(object, &pathLength);
        HashUpdate(hash, (const byte*)path, pathLength);
        break;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        value = TYPE_ARRAY;
        HashUpdate(hash, &value, 1);
        for (index = 0; HeapCollectionGet(object, HeapBoxSize(index++), &item);)
        {
            /* TODO: Avoid recursion */
            HeapHash(item, hash);
        }
        break;

    case TYPE_FUTURE:
    case TYPE_INVALID:
        assert(false);
        break;
    }
}

boolean HeapEquals(vref object1, vref object2)
{
    size_t index;
    size_t size1;
    size_t size2;
    vref item1;
    vref item2;
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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        if (!HeapIsString(object2))
        {
            return false;
        }
        size1 = VStringLength(object1);
        size2 = VStringLength(object2);
        return size1 == size2 &&
            !memcmp(getString(object1), getString(object2), size1);

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        if (!HeapIsCollection(object2))
        {
            return false;
        }
        size1 = VCollectionSize(object1);
        size2 = VCollectionSize(object2);
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
    case TYPE_INVALID:
        break;
    }
    assert(false);
    return false;
}

int HeapCompare(vref object1, vref object2)
{
    int i1 = HeapUnboxInteger(object1);
    int i2 = HeapUnboxInteger(object2);
    return i1 == i2 ? 0 : i1 < i2 ? -1 : 1;
}


byte *HeapAlloc(VType type, size_t size)
{
    assert(size <= UINT32_MAX - 1);
    return heapAlloc(type, (uint32)size);
}

vref HeapFinishAlloc(byte *objectData)
{
    return heapFinishAlloc(objectData);
}


vref HeapBoxInteger(int value)
{
    assert(value == HeapUnboxInteger(
               refFromUint(((uint)value & INTEGER_LITERAL_MASK) |
                           INTEGER_LITERAL_MARK)));
    return refFromUint(((uint)value & INTEGER_LITERAL_MASK) |
                       INTEGER_LITERAL_MARK);
}

vref HeapBoxUint(uint value)
{
    assert(value <= INT_MAX);
    return HeapBoxInteger((int)value);
}

vref HeapBoxSize(size_t value)
{
    assert(value <= INT_MAX);
    return HeapBoxInteger((int)value);
}

int HeapUnboxInteger(vref object)
{
    assert(isInteger(object));
    return ((signed)uintFromRef(object) << INTEGER_LITERAL_SHIFT) >>
        INTEGER_LITERAL_SHIFT;
}

size_t HeapUnboxSize(vref object)
{
    assert(isInteger(object));
    assert(HeapUnboxInteger(object) >= 0);
    return (size_t)HeapUnboxInteger(object);
}

int HeapIntegerSign(vref object)
{
    int i = HeapUnboxInteger(object);
    if (i > 0)
    {
        return 1;
    }
    return i >> (sizeof(int) * 8 - 1);
}


vref HeapCreateString(const char *restrict string, size_t length)
{
    byte *restrict objectData;

    if (!length)
    {
        return HeapEmptyString;
    }

    objectData = HeapAlloc(TYPE_STRING, length + 1);
    memcpy(objectData, string, length);
    objectData[length] = 0;
    return HeapFinishAlloc(objectData);
}

vref HeapCreateUninitialisedString(size_t length, char **data)
{
    byte *objectData;
    assert(length);
    objectData = HeapAlloc(TYPE_STRING, length + 1);
    objectData[length] = 0;
    *(byte**)data = objectData;
    return HeapFinishAlloc(objectData);
}

vref HeapCreateWrappedString(const char *restrict string,
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

vref HeapCreateSubstring(vref string, size_t offset, size_t length)
{
    SubString *ss;
    byte *data;

    assert(!HeapIsFutureValue(string));
    assert(HeapIsString(string));
    assert(VStringLength(string) >= offset + length);
    if (!length)
    {
        return HeapEmptyString;
    }
    if (length == VStringLength(string))
    {
        return string;
    }
    switch (HeapGetObjectType(string))
    {
    case TYPE_STRING:
        break;

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
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
    case TYPE_FUTURE:
    case TYPE_INVALID:
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

boolean HeapIsString(vref object)
{
    assert(!HeapIsFutureValue(object));
    switch (HeapGetObjectType(object))
    {
    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        return true;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return false;

    case TYPE_FUTURE:
    case TYPE_INVALID:
        break;
    }
    assert(false);
    return false;
}

const char *HeapGetString(vref object)
{
    assert(HeapGetObjectType(object) == TYPE_STRING);
    return (const char*)HeapGetObjectData(object);
}

char *HeapWriteSubstring(vref object, size_t offset, size_t length,
                         char *dst)
{
    assert(VStringLength(object) >= offset + length);
    memcpy(dst, getString(object) + offset, length);
    return dst + length;
}

vref HeapStringIndexOf(vref text, size_t startOffset,
                       vref substring)
{
    size_t textLength = VStringLength(text);
    size_t subLength = VStringLength(substring);
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


vref HeapCreatePath(vref path)
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
    srcLength = VStringLength(path);
    /* TODO: Avoid malloc */
    temp = FileCreatePath(null, 0, src, srcLength, null, 0, &tempLength);
    if (tempLength != srcLength && memcmp(src, temp, srcLength))
    {
        path = HeapCreateString(temp, tempLength);
    }
    free(temp);
    return boxReference(TYPE_FILE, path);
}

const char *HeapGetPath(vref path, size_t *length)
{
    vref s = unboxReference(TYPE_FILE, path);
    *length = VStringLength(s);
    return getString(s);
}

boolean HeapIsFile(vref object)
{
    return HeapGetObjectType(object) == TYPE_FILE;
}

vref HeapPathFromParts(vref path, vref name, vref extension)
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
    vref result;

    assert(!HeapIsFutureValue(path));
    assert(!HeapIsFutureValue(name));
    assert(!HeapIsFutureValue(extension));
    assert(!path || HeapIsString(path) || HeapIsFile(path));
    assert(HeapIsString(name) || HeapIsFile(name));
    assert(!extension || HeapIsString(extension));

    if (path)
    {
        pathString = toString(path, &freePath);
        pathLength = VStringLength(path);
    }
    nameString = toString(name, &freeName);
    nameLength = VStringLength(name);
    if (extension)
    {
        extensionString = toString(extension, &freeExtension);
        extensionLength = VStringLength(extension);
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


/* TODO: Size limit. */
static void getAllFlattened(vref list, vref *restrict dst, size_t *size,
                            boolean *flattened)
{
    size_t i;
    size_t size2;
    const vref *restrict src;
    switch (HeapGetObjectType(list))
    {
    case TYPE_ARRAY:
        src = (const vref*)HeapGetObjectData(list);
        size2 = HeapGetObjectSize(list) / sizeof(vref);
        for (i = 0; i < size2; i++)
        {
            vref v = *src++;
            assert(!HeapIsFutureValue(v)); /* TODO */
            if (HeapIsCollection(v))
            {
                size_t s = 0;
                *flattened = true;
                getAllFlattened(v, dst, &s, flattened);
                *size += s;
                dst += s;
            }
            else
            {
                *dst++ = v;
                (*size)++;
            }
        }
        return;

    case TYPE_INTEGER_RANGE:
        Fail("TODO: getAllFlattened: TYPE_INTEGER_RANGE\n");

    case TYPE_CONCAT_LIST:
        src = (const vref*)HeapGetObjectData(list);
        size2 = HeapGetObjectSize(list) / sizeof(vref);
        for (i = 0; i < size2; i++)
        {
            size_t s = 0;
            getAllFlattened(*src++, dst, &s, flattened);
            *size += s;
            dst += s;
        }
        return;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
    case TYPE_FUTURE:
    case TYPE_INVALID:
    default:
        assert(false);
        return;
    }
}

/* TODO: Strip null */
vref HeapCreateFilelist(vref value)
{
    size_t size;
    size_t i;
    vref newValue;
    vref *data;
    boolean converted = false;
    assert(!HeapIsFutureValue(value)); /* TODO */
    for (;;)
    {
        VType type = HeapGetObjectType(value);
        if (!isCollectionType(type))
        {
            value = HeapCreatePath(value);
            return HeapCreateArray(&value, 1);
        }
        size = VCollectionSize(value);
        if (!size)
        {
            return HeapEmptyList;
        }
        if (size != 1)
        {
            break;
        }
        HeapCollectionGet(value, HeapBoxInteger(0), &value);
    }

    data = (vref*)HeapAlloc(TYPE_ARRAY, 0);
    size = 0;
    getAllFlattened(value, data, &size, &converted);
    if (!size)
    {
        heapAllocAbort((byte*)data);
        return HeapEmptyList;
    }
    newValue = heapFinishRealloc((byte*)data, size * sizeof(vref));
    for (i = 0; i < size; i++)
    {
        if (!HeapIsFile(data[i]))
        {
            converted = true;
            data[i] = HeapCreatePath(data[i]);
        }
    }
    /* When measured, it was faster to create a new array than to keep a
       non-array type. */
    if (!converted && HeapGetObjectType(value) == TYPE_ARRAY)
    {
        heapFree(newValue);
        return value;
    }
    return newValue;
}

static void createPath(const char *path, size_t length, void *userdata)
{
    HeapCreatePath(HeapCreateString(path, length));
    (*(size_t*)userdata)++;
}

vref HeapCreateFilelistGlob(const char *pattern, size_t length)
{
    vref object = heapTop();
    size_t count = 0;
    byte *objectData;
    vref *files;

    FileTraverseGlob(pattern, length, createPath, &count);
    if (!count)
    {
        return HeapEmptyList;
    }
    objectData = HeapAlloc(TYPE_ARRAY, count * sizeof(vref)); /* TODO: Filelist type */
    files = (vref*)objectData;
    while (count--)
    {
        assert(HeapIsString(object));
        object = heapNext(object);
        assert(HeapIsFile(object));
        *files++ = object;
        object = heapNext(object);
    }
    /* TODO: Sort filelist */
    return HeapFinishAlloc(objectData);
}


vref HeapCreateRange(vref lowObject, vref highObject)
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

boolean HeapIsRange(vref object)
{
    return HeapGetObjectType(object) == TYPE_INTEGER_RANGE;
}

vref HeapRangeLow(vref range)
{
    return HeapBoxInteger(((int*)HeapGetObjectData(range))[0]);
}

vref HeapRangeHigh(vref range)
{
    return HeapBoxInteger(((int*)HeapGetObjectData(range))[1]);
}


vref HeapSplit(vref string, vref delimiter, boolean removeEmpty,
               boolean trimLastIfEmpty)
{
    size_t length;
    size_t delimiterLength;
    size_t offset;
    size_t lastOffset;
    vref offsetref;
    vref value;
    intvector substrings;

    assert(HeapIsString(string));
    length = VStringLength(string);
    if (!length)
    {
        return HeapEmptyList;
    }
    delimiterLength = VStringLength(delimiter);
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


vref HeapCreateArray(const vref *values, size_t size)
{
    byte *data;
    size *= sizeof(vref);
    data = HeapAlloc(TYPE_ARRAY, size);
    memcpy(data, values, size);
    return HeapFinishAlloc(data);
}

vref HeapCreateArrayFromVector(const intvector *values)
{
    if (!IVSize(values))
    {
        return HeapEmptyList;
    }
    return HeapCreateArray((const vref*)IVGetPointer(values, 0),
                           IVSize(values));
}

vref HeapConcatList(vref list1, vref list2)
{
    byte *data;
    vref *subLists;

    assert(HeapIsCollection(list1));
    assert(HeapIsCollection(list2));
    if (!VCollectionSize(list1))
    {
        return list2;
    }
    if (!VCollectionSize(list2))
    {
        return list1;
    }
    data = HeapAlloc(TYPE_CONCAT_LIST, sizeof(vref) * 2);
    subLists = (vref*)data;
    subLists[0] = list1;
    subLists[1] = list2;
    return HeapFinishAlloc(data);
}

boolean HeapIsCollection(vref object)
{
    return isCollectionType(HeapGetObjectType(object));
}

boolean HeapCollectionGet(vref object, vref indexObject,
                          vref *restrict value)
{
    const vref *restrict data;
    const vref *restrict limit;
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
    if (index >= VCollectionSize(object))
    {
        return false;
    }
    switch (HeapGetObjectType(object))
    {
    case TYPE_ARRAY:
        data = (const vref*)HeapGetObjectData(object);
        *value = data[index];
        return true;

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(object);
        assert(i <= INT_MAX - 1);
        assert(!addOverflow((int)i, intData[0]));
        *value = HeapBoxInteger((int)i + intData[0]);
        return true;

    case TYPE_CONCAT_LIST:
        data = (const vref*)HeapGetObjectData(object);
        limit = data + HeapGetObjectSize(object);
        while (data < limit)
        {
            size = VCollectionSize(*data);
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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
    case TYPE_FUTURE:
    case TYPE_INVALID:
    default:
        assert(false);
        return false;
    }
}


typedef struct
{
    vref value;
    Instruction op;
} FutureValueUnary;

typedef struct
{
    vref value1;
    vref value2;
    Instruction op;
} FutureValueBinary;

static vref executeUnary(Instruction op, vref value)
{
    switch (op)
    {
    case OP_PUSH:
        return value;

    case OP_NOT:
        assert(value == HeapTrue || value == HeapFalse);
        return value == HeapFalse ? HeapTrue : HeapFalse;

    case OP_NEG:
        assert(HeapUnboxInteger(value) != INT_MIN);
        return HeapBoxInteger(-HeapUnboxInteger(value));
        break;

    case OP_INV:
        return HeapBoxInteger(~HeapUnboxInteger(value));

    case OP_FUNCTION:
    case OP_ITER_GET:
    case OP_NULL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EMPTY_LIST:
    case OP_LIST:
    case OP_FILELIST:
    case OP_COPY:
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

static vref executeBinaryPartial(Instruction op, vref object,
                                 vref value1, vref value2)
{
    switch (op)
    {
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
        if ((!HeapIsFutureValue(value1) && !VIsTruthy(value1)) ||
            (!HeapIsFutureValue(value2) && !VIsTruthy(value2)))
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

    case OP_FUNCTION:
    case OP_NULL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EMPTY_LIST:
    case OP_LIST:
    case OP_FILELIST:
    case OP_PUSH:
    case OP_COPY:
    case OP_LOAD_FIELD:
    case OP_STORE_FIELD:
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

static vref executeBinary(Instruction op,
                          vref value1, vref value2)
{
    byte *data;
    size_t size1;
    size_t size2;

    switch (op)
    {
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
        return VIsTruthy(value1) && VIsTruthy(value2) ? HeapTrue : HeapFalse;
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
        size1 = VStringLength(value2);
        size2 = VStringLength(value1);
        if (!size1 && !size2)
        {
            return HeapEmptyString;
        }
        data = HeapAlloc(TYPE_STRING, size1 + size2 + 1);
        VWriteString(value2, (char*)data);
        VWriteString(value1, (char*)data + size1);
        data[size1 + size2] = 0;
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
            assert((size_t)HeapUnboxInteger(value1) < VCollectionSize(value2));
            if (!HeapCollectionGet(value2, value1, &value1))
            {
                assert(false);
                return 0;
            }
        }
        return value1;

    case OP_RANGE:
        return HeapCreateRange(value2, value1);

    case OP_FUNCTION:
    case OP_PUSH:
    case OP_NULL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EMPTY_LIST:
    case OP_LIST:
    case OP_FILELIST:
    case OP_COPY:
    case OP_LOAD_FIELD:
    case OP_STORE_FIELD:
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

boolean HeapIsFutureValue(vref object)
{
    return object && HeapGetObjectType(object) == TYPE_FUTURE;
}

vref HeapCreateFutureValue(void)
{
    byte *data = HeapAlloc(TYPE_FUTURE, sizeof(FutureValueUnary));
    FutureValueUnary *future = (FutureValueUnary*)data;
    future->value = 0;
    future->op = OP_UNKNOWN_VALUE;
    return HeapFinishAlloc(data);
}

void HeapSetFutureValue(vref object, vref value)
{
    FutureValueUnary *future = (FutureValueUnary*)HeapGetObjectData(object);
    assert(HeapGetObjectSize(object) == sizeof(FutureValueUnary));
    assert(!future->value);
    assert(future->op == OP_UNKNOWN_VALUE);
    future->value = value;
    future->op = OP_PUSH;
}

vref HeapTryWait(vref object)
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
        future1->value = HeapTryWait(future1->value);
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
        future2->value1 = HeapTryWait(future2->value1);
        future2->value2 = HeapTryWait(future2->value2);
        return (HeapIsFutureValue(future2->value1) ||
                HeapIsFutureValue(future2->value2)) ?
            executeBinaryPartial(future2->op, object,
                                 future2->value1, future2->value2) :
            executeBinary(future2->op, future2->value1, future2->value2);
    }
}

vref HeapWait(vref object)
{
    object = HeapTryWait(object);
    while (HeapIsFutureValue(object))
    {
        WorkExecute();
        object = HeapTryWait(object);
    }
    return object;
}


vref HeapApplyUnary(Instruction op, vref value)
{
    byte *data;
    FutureValueUnary *future;

    value = HeapTryWait(value);
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

vref HeapApplyBinary(Instruction op,
                     vref value1, vref value2)
{
    byte *data;
    FutureValueBinary *future;

    value1 = HeapTryWait(value1);
    value2 = HeapTryWait(value2);
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
