#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bytevector.h"
#include "fail.h"
#include "file.h"
#include "hash.h"
#include "heap.h"
#include "instruction.h"
#include "intvector.h"
#include "math.h"
#include "parser.h"
#include "stringpool.h"
#include "work.h"

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

static vref heapFinishRealloc(byte *objectData, size_t size)
{
    assert(size);
    assert(size <= UINT_MAX - 1);
    assert(HeapPageFree == objectData);
    *(uint*)(objectData - OBJECT_OVERHEAD) = (uint)size;
    HeapPageFree += size;
    return HeapFinishAlloc(objectData);
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
        *(uint*)(HeapPageBase + sizeFromRef(object) + HEADER_SIZE) +
        sizeFromRef(object) + OBJECT_OVERHEAD);
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
    VType type;

start:
    assert(!HeapIsFutureValue(object));

    type = HeapGetObjectType(object);
    switch ((int)type)
    {
    case TYPE_STRING:
        return (const char*)HeapGetObjectData(object);

    case TYPE_STRING_WRAPPED:
        return *(const char**)HeapGetObjectData(object);

    case TYPE_SUBSTRING:
        ss = (const SubString*)HeapGetObjectData(object);
        return &getString(ss->string)[ss->offset];

    case TYPE_VALUE:
        object = *(vref*)HeapGetObjectData(object);
        goto start;
    }
    unreachable;
}

static const char *toString(vref object, bool *copy)
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
    VNewline = HeapCreateString("\n", 1);
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
    case TYPE_VALUE:                 type = "value";          string = false; break;
    case TYPE_NULL:                  type = "null";           string = false; break;
    case TYPE_BOOLEAN_TRUE:          type = "true";           string = false; break;
    case TYPE_BOOLEAN_FALSE:         type = "false";          string = false; break;
    case TYPE_STRING:                type = "string";                         break;
    case TYPE_STRING_WRAPPED:        type = "string_wrapped";                 break;
    case TYPE_SUBSTRING:             type = "substring";                      break;
    case TYPE_FILE:                  type = "file";                           break;
    case TYPE_ARRAY:                 type = "array";                          break;
    case TYPE_INTEGER_RANGE:         type = "range";                          break;
    case TYPE_CONCAT_LIST:           type = "concat_list";                    break;
    case TYPE_FUTURE:                type = "future";                         break;
    case TYPE_FUTURE_NOT:            type = "future_not";                     break;
    case TYPE_FUTURE_NEG:            type = "future_neg";                     break;
    case TYPE_FUTURE_INV:            type = "future_inv";                     break;
    case TYPE_FUTURE_EQUALS:         type = "future_equals";                  break;
    case TYPE_FUTURE_NOT_EQUALS:     type = "future_not_equals";              break;
    case TYPE_FUTURE_LESS:           type = "future_less";                    break;
    case TYPE_FUTURE_LESS_EQUALS:    type = "future_less_equals";             break;
    case TYPE_FUTURE_AND:            type = "future_and";                     break;
    case TYPE_FUTURE_ADD:            type = "future_add";                     break;
    case TYPE_FUTURE_SUB:            type = "future_sub";                     break;
    case TYPE_FUTURE_MUL:            type = "future_mul";                     break;
    case TYPE_FUTURE_DIV:            type = "future_div";                     break;
    case TYPE_FUTURE_REM:            type = "future_rem";                     break;
    case TYPE_FUTURE_VALID_INDEX:    type = "future_valid_index";             break;
    case TYPE_FUTURE_INDEXED_ACCESS: type = "future_indexed_access";          break;
    case TYPE_FUTURE_RANGE:          type = "future_range";                   break;
    case TYPE_FUTURE_CONCAT:         type = "future_concat";                  break;
    case TYPE_FUTURE_CONCAT_STRING:  type = "future_concat_string";           break;

    case TYPE_INVALID:
    case TYPE_INTEGER:
    default:
        printf("%d type:%d\n", value, ho.type);
        unreachable;
    }
    BVAddData(&buffer, (const byte*)type, strlen(type));

    if (HeapIsFutureValue(value))
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
        if (HeapIsString(value))
        {
            BVAdd(&buffer, '\"');
        }
        else if (HeapIsFile(value))
        {
            BVAddData(&buffer, (const byte*)"@\"", 2);
        }
        VWriteString(value, (char*)BVGetAppendPointer(&buffer, length));
        if (HeapIsString(value) || HeapIsFile(value))
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

start:
    assert(!HeapIsFutureValue(object));

    type = HeapGetObjectType(object);
    switch ((int)type)
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
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        value = TYPE_STRING;
        HashUpdate(hash, &value, 1);
        HashUpdate(hash, (const byte*)getString(object),
                   VStringLength(object));
        return;

    case TYPE_FILE:
        value = TYPE_FILE;
        HashUpdate(hash, &value, 1);
        path = HeapGetPath(object, &pathLength);
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

    case TYPE_VALUE:
        object = *(vref*)HeapGetObjectData(object);
        goto start;
    }
    unreachable;
}


vref HeapCreateString(const char *restrict string, size_t length)
{
    byte *restrict objectData;

    if (!length)
    {
        return VEmptyString;
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
        return VEmptyString;
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
    VType type;

    assert(!HeapIsFutureValue(string));
    assert(HeapIsString(string));
    assert(VStringLength(string) >= offset + length);
    if (!length)
    {
        return VEmptyString;
    }
    if (length == VStringLength(string))
    {
        return string;
    }
start:
    type = HeapGetObjectType(string);
    switch ((int)type)
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

    case TYPE_VALUE:
        string = *(vref*)HeapGetObjectData(string);
        goto start;

    default:
        unreachable;
    }
    data = HeapAlloc(TYPE_SUBSTRING, sizeof(SubString));
    ss = (SubString*)data;
    ss->string = string;
    ss->offset = offset;
    ss->length = length;
    return HeapFinishAlloc(data);
}

vref HeapCreateStringFormatted(const char *format, va_list ap)
{
    char *data = (char*)HeapAlloc(TYPE_STRING, 0);
    char *start = data;
    while (*format)
    {
        const char *stop = format;
        while (*stop && *stop != '%')
        {
            stop++;
        }
        memcpy(data, format, (size_t)(stop - format));
        data += stop - format;
        format = stop;
        if (*format == '%')
        {
            format++;
            switch (*format++)
            {
            case 'c':
            {
                int c = va_arg(ap, int);
                if (c >= ' ' && c <= '~')
                {
                    *data++ = (char)c;
                }
                else if (c == '\n')
                {
                    *data++ = '\\';
                    *data++ = 'n';
                }
                else
                {
                    *data++ = '?';
                }
                break;
            }

            case 'd':
            {
                int value = va_arg(ap, int);
                char *numberStart;
                int i;
                if (value < 0)
                {
                    *data++ = '-';
                }
                numberStart = data;
                do
                {
                    *data++ = (char)('0' + (value < 0 ? -(value % 10) : value % 10));
                    value /= 10;
                }
                while (value);
                for (i = 0; numberStart + i < data - i - 1; i++)
                {
                    char tmp = numberStart[i];
                    numberStart[i] = data[-i - 1];
                    data[-i - 1] = tmp;
                }
                break;
            }

            case 's':
            {
                const char *string = va_arg(ap, const char*);
                size_t length = strlen(string);
                memcpy(data, string, length);
                data += length;
                break;
            }

            default:
                format--;
            }
        }
    }
    *data++ = 0;
    return heapFinishRealloc((byte*)start, (size_t)(data - start));
}

bool HeapIsString(vref object)
{
    VType type;
start:
    assert(!HeapIsFutureValue(object));
    type = HeapGetObjectType(object);
    switch ((int)type)
    {
    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
        return true;

    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return false;

    case TYPE_VALUE:
        object = *(vref*)HeapGetObjectData(object);
        goto start;
    }
    unreachable;
}

const char *HeapGetString(vref object)
{
    assert(HeapGetObjectType(object) == TYPE_STRING);
    return (const char*)HeapGetObjectData(object);
}

char *HeapGetStringCopy(vref object)
{
    size_t length = VStringLength(object);
    char *copy = (char*)malloc(length + 1);
    VWriteString(object, copy);
    copy[length] = 0;
    return copy;
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
        return VNull;
    }
    while (p < plimit)
    {
        p = (const char*)memchr(p, *s, (size_t)(plimit - p));
        if (!p)
        {
            return VNull;
        }
        if (!memcmp(p, s, subLength))
        {
            return VBoxSize((size_t)(p - pstart));
        }
        p++;
    }
    return VNull;
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

bool HeapIsFile(vref object)
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
    bool freePath = false;
    bool freeName;
    bool freeExtension = false;
    char *resultPath;
    size_t resultPathLength;
    vref result;

    assert(!HeapIsFutureValue(path));
    assert(!HeapIsFutureValue(name));
    assert(!HeapIsFutureValue(extension));
    assert(path == VNull || HeapIsString(path) || HeapIsFile(path));
    assert(HeapIsString(name) || HeapIsFile(name));
    assert(extension == VNull || HeapIsString(extension));

    if (path != VNull)
    {
        pathString = toString(path, &freePath);
        pathLength = VStringLength(path);
    }
    nameString = toString(name, &freeName);
    nameLength = VStringLength(name);
    if (extension != VNull)
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
                            bool *flattened)
{
    size_t i;
    size_t size2;
    const vref *restrict src;
    VType type;

start:
    type = HeapGetObjectType(list);
    switch ((int)type)
    {
    case TYPE_ARRAY:
        src = (const vref*)HeapGetObjectData(list);
        size2 = HeapGetObjectSize(list) / sizeof(vref);
        for (i = 0; i < size2; i++)
        {
            vref v = *src++;
            while (HeapGetObjectType(v) == TYPE_VALUE)
            {
                v = *(vref*)HeapGetObjectData(v);
            }
            assert(!HeapIsFutureValue(v));
            if (VIsCollection(v))
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

    case TYPE_VALUE:
        list = *(vref*)HeapGetObjectData(list);
        goto start;
    }
    unreachable;
}

/* TODO: Strip null */
vref HeapCreateFilelist(vref value)
{
    size_t size;
    size_t i;
    vref newValue;
    vref *data;
    bool converted = false;
    assert(!HeapIsFutureValue(value)); /* TODO */
    for (;;)
    {
        VType type = HeapGetObjectType(value);
        if (!VIsCollectionType(type))
        {
            value = HeapCreatePath(value);
            return VCreateArrayFromData(&value, 1);
        }
        size = VCollectionSize(value);
        if (!size)
        {
            return VEmptyList;
        }
        if (size != 1)
        {
            break;
        }
        VCollectionGet(value, VBoxInteger(0), &value);
    }

    data = (vref*)HeapAlloc(TYPE_ARRAY, 0);
    size = 0;
    getAllFlattened(value, data, &size, &converted);
    if (!size)
    {
        heapAllocAbort((byte*)data);
        return VEmptyList;
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
    vref *array;
    vref *files;

    FileTraverseGlob(pattern, length, createPath, &count);
    if (!count)
    {
        return VEmptyList;
    }
    array = VCreateArray(count); /* TODO: Filelist type */
    files = array;
    while (count--)
    {
        assert(HeapIsString(object));
        object = heapNext(object);
        assert(HeapIsFile(object));
        *files++ = object;
        object = heapNext(object);
    }
    /* TODO: Sort filelist */
    return VFinishArray(array);
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

    assert(HeapIsString(string));
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
                          intFromRef(HeapCreateSubstring(string, lastOffset, offset - lastOffset)));
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
        IVAdd(&substrings, intFromRef(HeapCreateSubstring(string, lastOffset,
                                                          length - lastOffset)));
    }
    value = VCreateArrayFromVector(&substrings);
    IVDispose(&substrings);
    IVSetSize(&ivtemp, oldTempSize);
    return value;
}


bool HeapIsFutureValue(vref object)
{
    return (HeapGetObjectType(object) & TYPE_FLAG_FUTURE) != 0;
}

vref HeapCreateFutureValue(void)
{
    byte *data = HeapAlloc(TYPE_FUTURE, sizeof(vref));
    *(vref*)data = 0;
    return HeapFinishAlloc(data);
}

void HeapSetFutureValue(vref object, vref value)
{
    byte *data = HeapPageBase + sizeFromRef(object);
    if (DEBUG_FUTURE)
    {
        char *value1 = HeapDebug(object);
        char *value2 = HeapDebug(value);
        printf("set %s = %s\n", value1, value2);
        free(value1);
        free(value2);
    }
    assert(HeapIsFutureValue(object));
    *(VType*)(data + HEADER_TYPE) = TYPE_VALUE;
    *(vref*)(data + OBJECT_OVERHEAD) = value;
}
