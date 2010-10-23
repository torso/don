#include "common.h"
#include "inthashmap.h"
#include "namespace.h"

static inthashmap fieldIndex;
static inthashmap functionIndex;
static inthashmap targetIndex;

void NamespaceInit(void)
{
    IntHashMapInit(&fieldIndex, 10);
    IntHashMapInit(&functionIndex, 10);
    IntHashMapInit(&targetIndex, 10);
}

void NamespaceDispose(void)
{
    IntHashMapDispose(&fieldIndex);
    IntHashMapDispose(&functionIndex);
    IntHashMapDispose(&targetIndex);
}


void NamespaceAddField(stringref name, fieldref field)
{
    IntHashMapAdd(&fieldIndex, uintFromRef(name), uintFromRef(field));
}

void NamespaceAddFunction(stringref name, functionref function)
{
    IntHashMapAdd(&functionIndex, uintFromRef(name), uintFromRef(function));
}

void NamespaceAddTarget(stringref name, functionref target)
{
    NamespaceAddFunction(name, target);
    IntHashMapAdd(&targetIndex, uintFromRef(name), uintFromRef(target));
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
