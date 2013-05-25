#include "common.h"
#include "heap.h"
#include "math.h"

static size_t collectionSize(const HeapObject *ho)
{
    const byte *data;
    const int *intData;
    const vref *values;
    const vref *limit;
    size_t size;

    switch (ho->type)
    {
    case TYPE_ARRAY:
        return ho->size / sizeof(vref);

    case TYPE_INTEGER_RANGE:
        intData = (const int*)ho->data;
        assert(!subOverflow(intData[1], intData[0]));
        return (size_t)(intData[1] - intData[0]) + 1;

    case TYPE_CONCAT_LIST:
        data = ho->data;
        values = (const vref*)data;
        limit = (const vref*)(data + ho->size);
        size = 0;
        while (values < limit)
        {
            size += VCollectionSize(*values++);
        }
        return size;

    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_STRING:
    case TYPE_STRING_WRAPPED:
    case TYPE_SUBSTRING:
    case TYPE_FILE:
    case TYPE_FUTURE:
    default:
        assert(false);
        return 0;
    }
}

VBool VGetBool(vref value)
{
    value = HeapTryWait(value);
    if (value == HeapTrue)
    {
        return TRUTHY;
    }
    if (!value || value == HeapFalse)
    {
        return FALSY;
    }
    if (HeapIsFutureValue(value))
    {
        return FUTURE;
    }
    if (HeapGetObjectType(value) == TYPE_INTEGER)
    {
        return HeapUnboxInteger(value) ? TRUTHY : FALSY;
    }
    if (HeapIsString(value))
    {
        return VStringLength(value) ? TRUTHY : FALSY;
    }
    if (HeapIsCollection(value))
    {
        return VCollectionSize(value) ? TRUTHY : FALSY;
    }
    return TRUTHY;
}

boolean VIsTruthy(vref value)
{
    return VGetBool(value) == TRUTHY;
}

boolean VIsFalsy(vref value)
{
    return VGetBool(value) == FALSY;
}


size_t VStringLength(vref value)
{
    uint i;
    size_t size;
    size_t index;
    vref item;
    HeapObject ho;

    assert(!HeapIsFutureValue(value));
    if (!value)
    {
        return 4;
    }
    HeapGet(value, &ho);
    switch (ho.type)
    {
    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(value);
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

    case TYPE_STRING_WRAPPED:
        return *(size_t*)&ho.data[sizeof(const char**)];

    case TYPE_SUBSTRING:
        return ((const SubString*)ho.data)->length;

    case TYPE_FILE:
        return VStringLength(*(ref_t*)ho.data);

    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
    case TYPE_CONCAT_LIST:
        size = collectionSize(&ho);
        if (size)
        {
            size--;
        }
        size = size + 2;
        for (index = 0; HeapCollectionGet(value, HeapBoxSize(index++), &item);)
        {
            size += VStringLength(item);
        }
        return size;

    case TYPE_FUTURE:
        break;
    }
    assert(false);
    return 0;
}

size_t VCollectionSize(vref value)
{
    HeapObject ho;

    assert(!HeapIsFutureValue(value));
    HeapGet(value, &ho);
    return collectionSize(&ho);
}
