#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "bytevector.h"
#include "debug.h"
#include "fail.h"
#include "file.h"
#include "hash.h"
#include "heap.h"
#include "math.h"
#include "vm.h"

vref VNull;
vref VTrue;
vref VFalse;
vref VEmptyString;
vref VEmptyList;
vref VNewline;
vref VFuture;

static intvector ivtemp;

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

static const char *toString(vref object, bool *copy)
{
    *copy = false;
    if (VIsString(object))
    {
        return getString(object);
    }
    if (VIsFile(object))
    {
        return getString(unboxReference(TYPE_FILE, object));
    }
    assert(false); /* TODO */
    return null;
}


void VInit(void)
{
    byte *p;

    IVInit(&ivtemp, 128);
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

void VDispose(void)
{
    IVDispose(&ivtemp);
}


char *VDebug(vref value)
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

void VHash(vref object, HashState *hash)
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
            VHash(item, hash);
        }
        return;

    case TYPE_INVALID:
    case TYPE_FUTURE:
        break;
    }
    unreachable;
}

VBool VGetBool(vref value)
{
    switch (HeapGetObjectType(value))
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_FILE:
        return TRUTHY;
    case TYPE_BOOLEAN_FALSE:
    case TYPE_NULL:
        return FALSY;

    case TYPE_INTEGER:
        return VUnboxInteger(value) ? TRUTHY : FALSY;

    case TYPE_STRING:
    case TYPE_SUBSTRING:
        return VStringLength(value) ? TRUTHY : FALSY;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return VCollectionSize(value) ? TRUTHY : FALSY;

    case TYPE_FUTURE:
        return FUTURE;

    case TYPE_INVALID:
        break;
    }
    unreachable;
}

bool VIsTruthy(vref value)
{
    return VGetBool(value) == TRUTHY;
}

bool VIsFalsy(vref value)
{
    return VGetBool(value) == FALSY;
}


#define INTEGER_LITERAL_MARK (((uint)1 << (sizeof(vref) * 8 - 1)))
#define INTEGER_LITERAL_MASK (~INTEGER_LITERAL_MARK)
#define INTEGER_LITERAL_SHIFT 1

pureconst bool VIsInteger(vref object)
{
    return (uintFromRef(object) & INTEGER_LITERAL_MARK) != 0;
}

vref VBoxInteger(int value)
{
    assert(value == VUnboxInteger(
               refFromUint(((uint)value & INTEGER_LITERAL_MASK) |
                           INTEGER_LITERAL_MARK)));
    return refFromUint(((uint)value & INTEGER_LITERAL_MASK) |
                       INTEGER_LITERAL_MARK);
}

vref VBoxUint(uint value)
{
    assert(value <= INT_MAX);
    return VBoxInteger((int)value);
}

vref VBoxSize(size_t value)
{
    assert(value <= INT_MAX);
    return VBoxInteger((int)value);
}

int VUnboxInteger(vref object)
{
    assert(VIsInteger(object));
    return ((signed)uintFromRef(object) << INTEGER_LITERAL_SHIFT) >>
        INTEGER_LITERAL_SHIFT;
}

size_t VUnboxSize(vref object)
{
    assert(VIsInteger(object));
    assert(VUnboxInteger(object) >= 0);
    return (size_t)VUnboxInteger(object);
}


bool VIsStringType(VType type)
{
    switch (type)
    {
    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return false;

    case TYPE_STRING:
    case TYPE_SUBSTRING:
        return true;

    case TYPE_INVALID:
    case TYPE_FUTURE:
        break;
    }
    unreachable;
}

bool VIsString(vref object)
{
    assert(object != VFuture);
    return VIsStringType(HeapGetObjectType(object));
}

size_t VStringLength(vref value)
{
    uint i;
    size_t size;
    size_t index;
    vref item;
    HeapObject ho;

    HeapGet(value, &ho);
    switch (ho.type)
    {
    case TYPE_NULL:
        return 4;

    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        i = (uint)VUnboxInteger(value);
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
        return ho.size - 1;

    case TYPE_SUBSTRING:
        return ((const SubString*)ho.data)->length;

    case TYPE_FILE:
        return VStringLength(*(ref_t*)ho.data);

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        size = VCollectionSize(value);
        if (size)
        {
            size--;
        }
        size = size + 6;
        for (index = 0; VCollectionGet(value, VBoxSize(index++), &item);)
        {
            size += VStringLength(item);
        }
        return size;

    case TYPE_INVALID:
    case TYPE_FUTURE:
        break;
    }
    unreachable;
}

vref VCreateString(const char *restrict string, size_t length)
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

vref VCreateUninitialisedString(size_t length, char **data)
{
    byte *objectData;
    assert(length);
    objectData = HeapAlloc(TYPE_STRING, length + 1);
    objectData[length] = 0;
    *(byte**)data = objectData;
    return HeapFinishAlloc(objectData);
}

vref VCreateSubstring(vref string, size_t offset, size_t length)
{
    SubString *ss;
    byte *data;
    VType type;

    assert(string != VFuture);
    assert(VIsString(string));
    assert(VStringLength(string) >= offset + length);
    if (!length)
    {
        return VEmptyString;
    }
    if (length == VStringLength(string))
    {
        return string;
    }

    type = HeapGetObjectType(string);
    switch ((int)type)
    {
    case TYPE_STRING:
        break;

    case TYPE_SUBSTRING:
        ss = (SubString*)HeapGetObjectData(string);
        string = ss->string;
        offset += ss->offset;
        break;

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

vref VCreateStringFormatted(const char *format, va_list ap)
{
    /* TODO: Calculate size first - make sure it fits on heap. */
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
    return HeapFinishRealloc((byte*)start, (size_t)(data - start));
}

const char *VGetString(vref object)
{
    assert(HeapGetObjectType(object) == TYPE_STRING);
    return (const char*)HeapGetObjectData(object);
}

char *VGetStringCopy(vref object)
{
    size_t length = VStringLength(object);
    char *copy = (char*)malloc(length + 1);
    VWriteString(object, copy);
    copy[length] = 0;
    return copy;
}

char *VWriteString(vref value, char *dst)
{
    size_t size;
    uint i;
    size_t index;
    vref item;
    HeapObject ho;
    const SubString *subString;

    assert(value != VFuture);
    HeapGet(value, &ho);
    switch (ho.type)
    {
    case TYPE_NULL:
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return dst;

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
        i = (uint)VUnboxInteger(value);
        if (!i)
        {
            *dst++ = '0';
            return dst;
        }
        size = VStringLength(value);
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
        memcpy(dst, ho.data, ho.size - 1);
        return dst + ho.size - 1;

    case TYPE_SUBSTRING:
        subString = (const SubString*)ho.data;
        return VWriteSubstring(subString->string, subString->offset, subString->length, dst);

    case TYPE_FILE:
        return VWriteString(*(vref*)ho.data, dst);

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        *dst++ = 'l';
        *dst++ = 'i';
        *dst++ = 's';
        *dst++ = 't';
        *dst++ = '(';
        for (index = 0; VCollectionGet(value, VBoxSize(index), &item);
             index++)
        {
            if (index)
            {
                *dst++ = ',';
            }
            dst = VWriteString(item, dst);
        }
        *dst++ = ')';
        return dst;

    case TYPE_INVALID:
    case TYPE_FUTURE:
        break;
    }
    unreachable;
}

char *VWriteSubstring(vref object, size_t offset, size_t length, char *dst)
{
    assert(VStringLength(object) >= offset + length);
    memcpy(dst, getString(object) + offset, length);
    return dst + length;
}

vref VStringIndexOf(vref text, size_t startOffset, vref substring)
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


vref VCreatePath(vref path)
{
    const char *src;
    size_t srcLength;
    char *temp;
    size_t tempLength;

    if (VIsFile(path))
    {
        return path;
    }
    src = getString(path);
    srcLength = VStringLength(path);
    /* TODO: Avoid malloc */
    temp = FileCreatePath(null, 0, src, srcLength, null, 0, &tempLength);
    if (tempLength != srcLength || memcmp(src, temp, srcLength))
    {
        path = VCreateString(temp, tempLength);
    }
    free(temp);
    return boxReference(TYPE_FILE, path);
}

const char *VGetPath(vref path, size_t *length)
{
    vref s = unboxReference(TYPE_FILE, path);
    *length = VStringLength(s);
    return getString(s);
}

bool VIsFile(vref object)
{
    return HeapGetObjectType(object) == TYPE_FILE;
}

vref VPathFromParts(vref path, vref name, vref extension)
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

    assert(path != VFuture);
    assert(name != VFuture);
    assert(extension != VFuture);
    assert(path == VNull || VIsString(path) || VIsFile(path));
    assert(VIsString(name) || VIsFile(name));
    assert(extension == VNull || VIsString(extension));

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
    result = VCreatePath(VCreateString(resultPath, resultPathLength));
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

    type = HeapGetObjectType(list);
    switch ((int)type)
    {
    case TYPE_ARRAY:
        src = (const vref*)HeapGetObjectData(list);
        size2 = HeapGetObjectSize(list) / sizeof(vref);
        for (i = 0; i < size2; i++)
        {
            vref v = *src++;
            assert(v != VFuture);
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
    }
    unreachable;
}

/* TODO: Strip null */
vref VCreateFilelist(vref value)
{
    size_t size;
    size_t i;
    vref newValue;
    vref *data;
    bool converted = false;
    assert(value != VFuture); /* TODO */
    for (;;)
    {
        VType type = HeapGetObjectType(value);
        if (!VIsCollectionType(type))
        {
            value = VCreatePath(value);
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
        HeapAllocAbort((byte*)data);
        return VEmptyList;
    }
    newValue = HeapFinishRealloc((byte*)data, size * sizeof(vref));
    for (i = 0; i < size; i++)
    {
        if (!VIsFile(data[i]))
        {
            converted = true;
            data[i] = VCreatePath(data[i]);
        }
    }
    /* When measured, it was faster to create a new array than to keep a
       non-array type. */
    if (!converted && HeapGetObjectType(value) == TYPE_ARRAY)
    {
        HeapFree(newValue);
        return value;
    }
    return newValue;
}

static void createPath(const char *path, size_t length, void *userdata)
{
    VCreatePath(VCreateString(path, length));
    (*(size_t*)userdata)++;
}

vref VCreateFilelistGlob(const char *pattern, size_t length)
{
    vref object = HeapTop();
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
        assert(VIsString(object));
        object = HeapNext(object);
        assert(VIsFile(object));
        *files++ = object;
        object = HeapNext(object);
    }
    /* TODO: Sort filelist */
    return VFinishArray(array);
}


vref *VCreateArray(size_t size)
{
    return (vref*)HeapAlloc(TYPE_ARRAY, size * sizeof(vref));
}

vref VFinishArray(vref *array)
{
    return HeapFinishAlloc((byte*)array);
}

vref VCreateArrayFromData(const vref *values, size_t size)
{
    byte *data;
    size *= sizeof(vref);
    data = HeapAlloc(TYPE_ARRAY, size);
    memcpy(data, values, size);
    return HeapFinishAlloc(data);
}

vref VCreateArrayFromVector(const intvector *values)
{
    return VCreateArrayFromVectorSegment(values, 0, IVSize(values));
}

vref VCreateArrayFromVectorSegment(const intvector *values,
                                      size_t start, size_t length)
{
    if (!length)
    {
        return VEmptyList;
    }
    return VCreateArrayFromData((const vref*)IVGetPointer(values, start), length);
}

vref VConcatList(vref list1, vref list2)
{
    byte *data;
    vref *subLists;

    assert(VIsCollection(list1));
    assert(VIsCollection(list2));
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

bool VIsCollectionType(VType type)
{
    switch ((int)type)
    {
    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
        return false;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return true;
    }
    unreachable;
}

bool VIsCollection(vref object)
{
    return VIsCollectionType(HeapGetObjectType(object));
}

size_t VCollectionSize(vref value)
{
    const byte *data;
    const int *intData;
    const vref *values;
    const vref *limit;
    size_t size;
    HeapObject ho;

    assert(value != VFuture);
    HeapGet(value, &ho);
    switch ((int)ho.type)
    {
    case TYPE_ARRAY:
        return ho.size / sizeof(vref);

    case TYPE_INTEGER_RANGE:
        intData = (const int*)ho.data;
        assert(!subOverflow(intData[1], intData[0]));
        return (size_t)(intData[1] - intData[0]) + 1;

    case TYPE_CONCAT_LIST:
        data = ho.data;
        values = (const vref*)data;
        limit = (const vref*)(data + ho.size);
        size = 0;
        while (values < limit)
        {
            size += VCollectionSize(*values++);
        }
        return size;
    }
    unreachable;
}

bool VCollectionGet(vref object, vref indexObject, vref *restrict value)
{
    const vref *restrict limit;
    const int *restrict intData;
    ssize_t i;
    size_t index;
    size_t size;
    VType type;

    assert(object != VFuture);
    assert(indexObject != VFuture);

    i = VUnboxInteger(indexObject);
    if (i < 0)
    {
        return false;
    }
    index = (size_t)i;
    if (index >= VCollectionSize(object))
    {
        return false;
    }
    type = HeapGetObjectType(object);
    switch ((int)type)
    {
    case TYPE_ARRAY:
    {
        vref *restrict data = (vref*)HeapGetObjectData(object);
        *value = data[index];
        return true;
    }

    case TYPE_INTEGER_RANGE:
        intData = (const int *)HeapGetObjectData(object);
        assert(i <= INT_MAX - 1);
        assert(!addOverflow((int)i, intData[0]));
        *value = VBoxInteger((int)i + intData[0]);
        return true;

    case TYPE_CONCAT_LIST:
    {
        const vref *restrict data = (const vref*)HeapGetObjectData(object);
        limit = data + HeapGetObjectSize(object);
        while (data < limit)
        {
            size = VCollectionSize(*data);
            if (index < size)
            {
                assert(index <= INT_MAX);
                return VCollectionGet(*data, VBoxSize(index), value);
            }
            index -= size;
            data++;
        }
        return false;
    }
    }
    unreachable;
}


static vref VRangeLow(vref range)
{
    return VBoxInteger(((int*)HeapGetObjectData(range))[0]);
}

static vref VRangeHigh(vref range)
{
    return VBoxInteger(((int*)HeapGetObjectData(range))[1]);
}


vref VEquals(vref value1, vref value2)
{
    VType type1, type2;

    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }
    if (value1 == value2)
    {
        return VTrue;
    }
    type1 = HeapGetObjectType(value1);
    type2 = HeapGetObjectType(value2);

    switch (type1)
    {
    case TYPE_NULL:
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
        return VFalse;

    case TYPE_STRING:
    case TYPE_SUBSTRING:
        if (!VIsStringType(type2))
        {
            return VFalse;
        }
        {
            size_t size1 = VStringLength(value1);
            size_t size2 = VStringLength(value2);
            return size1 == size2 &&
                !memcmp(getString(value1), getString(value2), size1) ? VTrue : VFalse;
        }

    case TYPE_FILE:
        return VFalse;

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        if (!VIsCollectionType(type2))
        {
            return VFalse;
        }
        {
            size_t size1 = VCollectionSize(value1);
            size_t size2 = VCollectionSize(value2);
            size_t index;
            if (size1 != size2)
            {
                return VFalse;
            }
            for (index = 0; index < size1; index++)
            {
                vref item1;
                vref item2;
                vref result;
                bool success =
                    VCollectionGet(value1, VBoxSize(index), &item1) &&
                    VCollectionGet(value2, VBoxSize(index), &item2);
                assert(success);
                result = VEquals(item1, item2);
                if (result != VTrue)
                {
                    return result;
                }
            }
        }
        return VTrue;

    case TYPE_FUTURE:
    case TYPE_INVALID:
    default:
        unreachable;
    }
}

vref VLess(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        return VUnboxInteger(value1) < VUnboxInteger(value2) ?
            VTrue : VFalse;
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot compare non-numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VLessEquals(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        return VUnboxInteger(value1) <= VUnboxInteger(value2) ?
            VTrue : VFalse;
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot compare non-numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VAdd(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        return VBoxInteger(VUnboxInteger(value1) + VUnboxInteger(value2));
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot add non-numbers. Use \"$a$b\" to concatenate strings";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VSub(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        return VBoxInteger(VUnboxInteger(value1) - VUnboxInteger(value2));
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot subtract non-numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VMul(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        return VBoxInteger(VUnboxInteger(value1) * VUnboxInteger(value2));
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot multiply non-numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VDiv(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        int i2 = VUnboxInteger(value2);
        if (!i2)
        {
            const char msg[] = "Division by zero";
            VMFail(vm, msg, sizeof(msg) - 1);
            return 0;
        }
        assert((VUnboxInteger(value1) / i2) * i2 == VUnboxInteger(value1)); /* TODO: fraction */
        return VBoxInteger(VUnboxInteger(value1) / i2);
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot divide non-numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VRem(VM *vm, vref value1, vref value2)
{
    if (VIsInteger(value1) && VIsInteger(value2))
    {
        int i2 = VUnboxInteger(value2);
        if (!i2)
        {
            const char msg[] = "Division by zero";
            VMFail(vm, msg, sizeof(msg) - 1);
            return 0;
        }
        return VBoxInteger(VUnboxInteger(value1) % i2);
    }
    if (value1 == VFuture || value2 == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Cannot divide non-numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VNot(vref value)
{
    switch (VGetBool(value))
    {
    case TRUTHY:
        return VFalse;
    case FALSY:
        return VTrue;
    case FUTURE:
        return VFuture;
    }
    unreachable;
}

vref VNeg(VM *vm, vref value)
{
    if (VIsInteger(value))
    {
        return VBoxInteger(-VUnboxInteger(value));
    }
    if (value == VFuture)
    {
        return VFuture;
    }
    {
        const char msg[] = "Cannot negate non-number";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VInv(VM *vm, vref value)
{
    if (VIsInteger(value))
    {
        return VBoxInteger(~VUnboxInteger(value));
    }
    if (value == VFuture)
    {
        return VFuture;
    }
    {
        const char msg[] = "Cannot invert non-number.";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VValidIndex(VM *vm, vref collection, vref index)
{
    VType type;

    if (!VIsInteger(index))
    {
        assert(index == VFuture); /* TODO */
        return VFuture;
    }

    type = HeapGetObjectType(collection);
    switch ((int)type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        return VUnboxSize(index) < VCollectionSize(collection) ? VTrue : VFalse;

    case TYPE_FUTURE:
        return VFuture;

    default:
    {
        const char msg[] = "Can't iterate over non-collection type";
        VMFail(vm, msg, sizeof(msg) - 1);
        return 0;
    }
    }
    unreachable;
}

vref VIndexedAccess(VM *vm, vref value1, vref value2)
{
    vref value;
    VType indexType = HeapGetObjectType(value2);
    VType type;

    switch ((int)indexType)
    {
    case TYPE_INTEGER:
    case TYPE_INTEGER_RANGE:
        break;
    case TYPE_FUTURE:
        return VFuture;
    default:
    {
        const char msg[] = "Index must be an integer";
        VMFail(vm, msg, sizeof(msg) - 1);
        return 0;
    }
    }

    type = HeapGetObjectType(value1);
    switch ((int)type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        if (indexType != TYPE_INTEGER)
        {
            /* TODO: Range */
            const char msg[] = "Index must be an integer";
            VMFail(vm, msg, sizeof(msg) - 1);
            return 0;
        }
        if (!VCollectionGet(value1, value2, &value))
        {
            const char msg[] = "Array index out of bounds";
            VMFail(vm, msg, sizeof(msg) - 1);
            return 0;
        }
        return value;

    case TYPE_STRING:
    case TYPE_SUBSTRING:
        if (indexType == TYPE_INTEGER_RANGE)
        {
            size_t size1 = VUnboxSize(VRangeLow(value2));
            size_t size2 = VUnboxSize(VRangeHigh(value2));
            assert(size2 >= size1); /* TODO: Support inverted ranges. */
            return VCreateSubstring(value1, size1, size2 - size1 + 1);
        }
        else
        {
            assert(indexType == TYPE_INTEGER);
            return VCreateSubstring(value1, VUnboxSize(value2), 1);
        }

    case TYPE_FUTURE:
        return VFuture;

    default:
    {
        const char msg[] = "Can't do indexed access on non-collection and non-string type";
        VMFail(vm, msg, sizeof(msg) - 1);
        return 0;
    }
    }
    unreachable;
}

vref VRange(VM *vm, vref lowValue, vref highValue)
{
    if (VIsInteger(lowValue) && VIsInteger(highValue))
    {
        byte *objectData;
        int *p;
        int low = VUnboxInteger(lowValue);
        int high = VUnboxInteger(highValue);

        assert(low <= high); /* TODO: Reverse range */
        assert(!subOverflow(high, low));
        objectData = HeapAlloc(TYPE_INTEGER_RANGE, 2 * sizeof(int));
        p = (int*)objectData;
        p[0] = low;
        p[1] = high;
        return HeapFinishAlloc(objectData);
    }
    if (lowValue == VFuture || highValue == VFuture)
    {
        return VFuture;
    }

    {
        const char msg[] = "Range operands must be numbers";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VConcat(VM *vm, vref value1, vref value2)
{
    VType type = HeapGetObjectType(value1);
    switch ((int)type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        break;
    case TYPE_FUTURE:
        return VFuture;
    default:
        goto error;
    }
    type = HeapGetObjectType(value2);
    switch ((int)type)
    {
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        break;
    case TYPE_FUTURE:
        return VFuture;
    default:
        goto error;
    }
    return VConcatList(value1, value2);

error:
    {
        const char msg[] = "Concat operands must be lists";
        VMFail(vm, msg, sizeof(msg) - 1);
    }
    return 0;
}

vref VConcatString(size_t count, vref *values)
{
    size_t i;
    size_t length = 0;
    vref string;
    char *data;

    for (i = 0; i < count; i++)
    {
        vref value = values[i];
        if (value == VFuture)
        {
            return VFuture;
        }
        length += VStringLength(value);
    }
    if (!length)
    {
        return VEmptyString;
    }
    string = VCreateUninitialisedString(length, &data);
    for (i = 0; i < count; i++)
    {
        data = VWriteString(values[i], data);
    }
    return string;
}


static vref VSplitOnCharacter(vref string, size_t length, char c,
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

vref VSplit(vref string, vref delimiter, bool removeEmpty, bool trimLastIfEmpty)
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
            return VSplitOnCharacter(string, length, c, removeEmpty, trimLastIfEmpty);
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
