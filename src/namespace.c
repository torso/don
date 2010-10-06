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
    return IntHashMapAdd(&fieldIndex, (uint)name, (uint)field);
}

ErrorCode NamespaceAddFunction(stringref name, functionref function)
{
    return IntHashMapAdd(&functionIndex, (uint)name, (uint)function);
}

ErrorCode NamespaceAddTarget(stringref name, functionref target)
{
    ErrorCode error = NamespaceAddFunction(name, target);
    if (error)
    {
        return error;
    }
    return IntHashMapAdd(&targetIndex, (uint)name, (uint)target);
}


fieldref NamespaceGetField(stringref name)
{
    return (fieldref)IntHashMapGet(&fieldIndex, (uint)name);
}

functionref NamespaceGetFunction(stringref name)
{
    return (functionref)IntHashMapGet(&functionIndex, (uint)name);
}

functionref NamespaceGetTarget(stringref name)
{
    return (functionref)IntHashMapGet(&targetIndex, (uint)name);
}
