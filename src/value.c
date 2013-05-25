#include "common.h"
#include "heap.h"

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
        return HeapCollectionSize(value) ? TRUTHY : FALSY;
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
        size = HeapCollectionSize(value);
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
