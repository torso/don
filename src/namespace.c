#include "common.h"
#include "inthashmap.h"
#include "namespace.h"

static inthashmap fieldIndex;
static inthashmap functionIndex;
static inthashmap targetIndex;

ErrorCode NamespaceInit(void)
{
    ErrorCode error;
    error = IntHashMapInit(&fieldIndex, 10);
    if (error)
    {
        return error;
    }
    error = IntHashMapInit(&functionIndex, 10);
    if (error)
    {
        return error;
    }
    return IntHashMapInit(&targetIndex, 10);
}

void NamespaceDispose(void)
{
    IntHashMapDispose(&fieldIndex);
    IntHashMapDispose(&functionIndex);
    IntHashMapDispose(&targetIndex);
}


ErrorCode NamespaceAddField(stringref name, fieldref field)
{
    return IntHashMapAdd(&fieldIndex, uintFromRef(name), uintFromRef(field));
}

ErrorCode NamespaceAddFunction(stringref name, functionref function)
{
    return IntHashMapAdd(&functionIndex,
                         uintFromRef(name), uintFromRef(function));
}

ErrorCode NamespaceAddTarget(stringref name, functionref target)
{
    ErrorCode error = NamespaceAddFunction(name, target);
    if (error)
    {
        return error;
    }
    return IntHashMapAdd(&targetIndex, uintFromRef(name), uintFromRef(target));
}


fieldref NamespaceGetField(stringref name)
{
    return refFromUint(IntHashMapGet(&fieldIndex, uintFromRef(name)));
}

functionref NamespaceGetFunction(stringref name)
{
    return refFromUint(IntHashMapGet(&functionIndex, uintFromRef(name)));
}

functionref NamespaceGetTarget(stringref name)
{
    return refFromUint(IntHashMapGet(&targetIndex, uintFromRef(name)));
}
