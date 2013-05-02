#include "common.h"
#include "instruction.h"
#include "heap.h"
#include "value.h"

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
        return HeapStringLength(value) ? TRUTHY : FALSY;
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
