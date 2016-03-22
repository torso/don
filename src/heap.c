#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "bytevector.h"
#include "debug.h"
#include "hash.h"
#include "heap.h"
#include "intvector.h"
#include "math.h"
#include "parser.h"
#include "stringpool.h"

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
static intvector ivtemp;


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


static const char *getString(vref object)
{
    const SubString *ss;
    VType type;

    assert(object != VFuture);

    type = HeapGetObjectType(object);
    switch ((int)type)
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(object);

    case TYPE_SUBSTRING:
        ss = (const SubString*)HeapGetObjectData(object);
        return &getString(ss->string)[ss->offset];
    }
    unreachable;
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
    byte *p;

    IVInit(&ivtemp, 128);
    HeapPageIndex = (byte**)malloc(INITIAL_HEAP_INDEX_SIZE * sizeof(*HeapPageIndex));
    HeapPageIndexSize = INITIAL_HEAP_INDEX_SIZE;
    HeapPageIndex[0] = (byte*)malloc(PAGE_SIZE);
    HeapPageBase = HeapPageIndex[0];
    HeapPageLimit = HeapPageIndex[0] + PAGE_SIZE;
    HeapPageFree = HeapPageIndex[0] + sizeof(int);
    HeapPageOffset = 0;
    StringPoolInit();
    ParserAddKeywords();
    VNull = HeapFinishAlloc(HeapAlloc(TYPE_NULL, 0));
    VTrue = HeapFinishAlloc(HeapAlloc(TYPE_BOOLEAN_TRUE, 0));
    VFalse = HeapFinishAlloc(HeapAlloc(TYPE_BOOLEAN_FALSE, 0));
    p = HeapAlloc(TYPE_STRING, 1);
    *p = 0;
    VEmptyString = HeapFinishAlloc(p);
    VEmptyList = HeapFinishAlloc(HeapAlloc(TYPE_ARRAY, 0));
    VNewline = VCreateString("\n", 1);
    VFuture = HeapFinishAlloc(HeapAlloc(TYPE_FUTURE, 0));
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
    IVDispose(&ivtemp);
}


char *HeapDebug(vref value)
{
    HeapObject ho;
    bytevector buffer;
    size_t length;
    const char *type;
    bool string = true;

    BVInit(&buffer, 64);

    if (VIsInteger(value))
    {
        length = BVGetReservedAppendSize(&buffer);
        snprintf((char*)BVGetAppendPointer(&buffer, length), length,
                 "[int=%d]", VUnboxInteger(value));
        BVSetSize(&buffer, strlen((const char*)BVGetPointer(&buffer, 0)));
        return (char*)BVDisposeContainer(&buffer);
    }

    length = BVGetReservedAppendSize(&buffer);
    snprintf((char*)BVGetAppendPointer(&buffer, length), length, "[%u:", value);
    BVSetSize(&buffer, strlen((const char*)BVGetPointer(&buffer, 0)));

    if (!value)
    {
        const char s[] = "invalid]";
        BVAddData(&buffer, (const byte*)s, sizeof(s));
        return (char*)BVDisposeContainer(&buffer);
    }
    HeapGet(value, &ho);
    switch (ho.type)
    {
    case TYPE_NULL:                  type = "null";           string = false; break;
    case TYPE_BOOLEAN_TRUE:          type = "true";           string = false; break;
    case TYPE_BOOLEAN_FALSE:         type = "false";          string = false; break;
    case TYPE_STRING:                type = "string";                         break;
    case TYPE_SUBSTRING:             type = "substring";                      break;
    case TYPE_FILE:                  type = "file";                           break;
    case TYPE_ARRAY:                 type = "array";                          break;
    case TYPE_INTEGER_RANGE:         type = "range";                          break;
    case TYPE_CONCAT_LIST:           type = "concat_list";                    break;
    case TYPE_FUTURE:                type = "future";                         break;

    case TYPE_INVALID:
    case TYPE_INTEGER:
    default:
        printf("%d type:%d\n", value, ho.type);
        unreachable;
    }
    BVAddData(&buffer, (const byte*)type, strlen(type));

    if (value == VFuture)
    {
        size_t i;
        const vref *data = (const vref*)ho.data;
        BVAdd(&buffer, ':');
        for (i = 0; i < ho.size / sizeof(vref); i++)
        {
            if (i != 0)
            {
                BVAdd(&buffer, ',');
            }
            length = BVGetReservedAppendSize(&buffer);
            if (VIsInteger(data[i]))
            {
                snprintf((char*)BVGetAppendPointer(&buffer, length), length,
                         "int=%d", VUnboxInteger(data[i]));
            }
            else
            {
                snprintf((char*)BVGetAppendPointer(&buffer, length), length, "%u", data[i]);
            }
            BVSetSize(&buffer, strlen((const char*)BVGetPointer(&buffer, 0)));
        }
    }
    else if (string)
    {
        BVAdd(&buffer, ':');
        length = VStringLength(value);
        if (VIsString(value))
        {
            BVAdd(&buffer, '\"');
        }
        else if (VIsFile(value))
        {
            BVAddData(&buffer, (const byte*)"@\"", 2);
        }
        VWriteString(value, (char*)BVGetAppendPointer(&buffer, length));
        if (VIsString(value) || VIsFile(value))
        {
            BVAdd(&buffer, '\"');
        }
    }
    BVAddData(&buffer, (const byte*)"]", 2);
    return (char*)BVDisposeContainer(&buffer);
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

void HeapHash(vref object, HashState *hash)
{
    byte value;
    const char *path;
    size_t pathLength;
    size_t index;
    vref item;
    VType type;

    assert(object != VFuture);

    type = HeapGetObjectType(object);
    switch (type)
    {
    case TYPE_NULL:
        value = 0;
        HashUpdate(hash, &value, 1);
        return;

    case TYPE_BOOLEAN_TRUE:
        value = TYPE_BOOLEAN_TRUE;
        HashUpdate(hash, &value, 1);
        return;

    case TYPE_BOOLEAN_FALSE:
        value = TYPE_BOOLEAN_FALSE;
        HashUpdate(hash, &value, 1);
        return;

    case TYPE_INTEGER:
        value = TYPE_INTEGER;
        HashUpdate(hash, &value, 1);
        /* TODO: Make platform independent. */
        HashUpdate(hash, (const byte*)&object, sizeof(object));
        return;

    case TYPE_STRING:
    case TYPE_SUBSTRING:
        value = TYPE_STRING;
        HashUpdate(hash, &value, 1);
        HashUpdate(hash, (const byte*)getString(object),
                   VStringLength(object));
        return;

    case TYPE_FILE:
        value = TYPE_FILE;
        HashUpdate(hash, &value, 1);
        path = VGetPath(object, &pathLength);
        HashUpdate(hash, (const byte*)path, pathLength);
        return;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        value = TYPE_ARRAY;
        HashUpdate(hash, &value, 1);
        for (index = 0; VCollectionGet(object, VBoxSize(index++), &item);)
        {
            /* TODO: Avoid recursion */
            HeapHash(item, hash);
        }
        return;

    case TYPE_INVALID:
    case TYPE_FUTURE:
        break;
    }
    unreachable;
}


vref HeapCreateRange(vref lowObject, vref highObject)
{
    byte *objectData;
    int *p;
    int low = VUnboxInteger(lowObject);
    int high = VUnboxInteger(highObject);

    assert(low <= high); /* TODO: Reverse range */
    assert(!subOverflow(high, low));
    objectData = HeapAlloc(TYPE_INTEGER_RANGE, 2 * sizeof(int));
    p = (int*)objectData;
    p[0] = low;
    p[1] = high;
    return HeapFinishAlloc(objectData);
}

bool HeapIsRange(vref object)
{
    return HeapGetObjectType(object) == TYPE_INTEGER_RANGE;
}

vref HeapRangeLow(vref range)
{
    return VBoxInteger(((int*)HeapGetObjectData(range))[0]);
}

vref HeapRangeHigh(vref range)
{
    return VBoxInteger(((int*)HeapGetObjectData(range))[1]);
}


static vref HeapSplitOnCharacter(vref string, size_t length, char c,
                                 bool removeEmpty, bool trimLastIfEmpty)
{
    intvector substrings;
    const char *pstring = getString(string);
    const char *current = pstring;
    const char *limit = pstring + length;
    vref value;

    IVInit(&substrings, 64);
    while (true)
    {
        const char *next = (const char*)memchr(current, c, (size_t)(limit - current));
        if (!next)
        {
            break;
        }
        if (next != current || !removeEmpty)
        {
            IVAdd(&substrings,
                  intFromRef(VCreateSubstring(string, (size_t)(current - pstring),
                                              (size_t)(next - current))));
        }
        current = next + 1;
    }
    if (current != pstring + length || !(removeEmpty || trimLastIfEmpty))
    {
        IVAdd(&substrings, intFromRef(VCreateSubstring(string, (size_t)(current - pstring),
                                                       (size_t)(pstring + length - current))));
    }
    value = VCreateArrayFromVector(&substrings);
    IVDispose(&substrings);
    return value;
}

vref HeapSplit(vref string, vref delimiter, bool removeEmpty,
               bool trimLastIfEmpty)
{
    size_t oldTempSize = IVSize(&ivtemp);
    size_t length;
    size_t delimiterCount;
    size_t offset;
    size_t lastOffset;
    vref value;
    intvector substrings;
    const char *pstring;
    const int *delimiterVector;
    const int *delimiterVectorLimit;

    assert(VIsString(string));
    length = VStringLength(string);
    if (!length)
    {
        return VEmptyList;
    }
    if (VIsCollection(delimiter))
    {
        size_t i;
        delimiterCount = VCollectionSize(delimiter);
        for (i = 0; i < delimiterCount; i++)
        {
            vref s;
            size_t delimiterLength;
            VCollectionGet(delimiter, VBoxSize(i), &s);
            delimiterLength = VStringLength(s);
            assert(delimiterLength <= INT_MAX);
            if (!delimiterLength || delimiterLength > length)
            {
                /* delimiterCount--; */
                continue;
            }
            IVAdd(&ivtemp, (int)delimiterLength);
            VWriteString(s, (char*)IVGetAppendPointer(
                             &ivtemp, (delimiterLength + sizeof(int) - 1) / sizeof(int)));
        }
        if (IVSize(&ivtemp) == oldTempSize)
        {
            return VCreateArrayFromData(&string, 1);
        }
    }
    else
    {
        size_t delimiterLength = VStringLength(delimiter);
        assert(delimiterLength <= INT_MAX);
        if (delimiterLength == 1)
        {
            char c;
            VWriteString(delimiter, &c);
            return HeapSplitOnCharacter(string, length, c, removeEmpty, trimLastIfEmpty);
        }
        if (!delimiterLength || delimiterLength > length)
        {
            return VCreateArrayFromData(&string, 1);
        }
        IVAdd(&ivtemp, (int)delimiterLength);
        VWriteString(delimiter, (char*)IVGetAppendPointer(
                         &ivtemp, (delimiterLength + sizeof(int) - 1) / sizeof(int)));
    }
    delimiterVector = IVGetPointer(&ivtemp, oldTempSize);
    delimiterVectorLimit = delimiterVector + IVSize(&ivtemp) - oldTempSize;

    IVInit(&substrings, 4);
    pstring = getString(string); /* TODO: Handle concatenated strings */
    offset = 0;
    lastOffset = 0;
continueMatching:
    while (offset < length)
    {
        const int *currentDelimiter = delimiterVector;
        while (currentDelimiter < delimiterVectorLimit)
        {
            size_t delimiterLength = (size_t)*currentDelimiter++;
            if (!memcmp(pstring + offset, currentDelimiter, delimiterLength))
            {
                if (offset != lastOffset || !removeEmpty)
                {
                    IVAdd(&substrings,
                          intFromRef(VCreateSubstring(string, lastOffset, offset - lastOffset)));
                }
                offset += delimiterLength;
                lastOffset = offset;
                goto continueMatching;
            }
            currentDelimiter += (delimiterLength + sizeof(int) - 1) / sizeof(int);
        }
        offset++;
    }
    if (length != lastOffset || !(removeEmpty || trimLastIfEmpty))
    {
        IVAdd(&substrings, intFromRef(VCreateSubstring(string, lastOffset,
                                                       length - lastOffset)));
    }
    value = VCreateArrayFromVector(&substrings);
    IVDispose(&substrings);
    IVSetSize(&ivtemp, oldTempSize);
    return value;
}
