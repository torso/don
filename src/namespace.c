#include "common.h"
#include "bytevector.h"
#include "inthashmap.h"
#include "namespace.h"

typedef struct
{
    inthashmap namespaces;
    inthashmap fieldIndex;
    inthashmap functionIndex;
    inthashmap targetIndex;
} Namespace;

static bytevector namespaceData;
static inthashmap fileNamespace;


static Namespace *getNamespace(namespaceref ns)
{
    return (Namespace*)ByteVectorGetPointer(
        &namespaceData, sizeFromRef(ns) * sizeof(Namespace));
}

static void disposeNamespace(Namespace *ns)
{
    IntHashMapDispose(&ns->namespaces);
    IntHashMapDispose(&ns->fieldIndex);
    IntHashMapDispose(&ns->functionIndex);
    IntHashMapDispose(&ns->targetIndex);
}

void NamespaceInit(void)
{
    ByteVectorInit(&namespaceData, sizeof(Namespace) * 4);
    IntHashMapInit(&fileNamespace, 4);
}

void NamespaceDispose(void)
{
    Namespace *ns;
    Namespace *limit = (Namespace*)ByteVectorGetAppendPointer(&namespaceData);

    for (ns = getNamespace(0); ns < limit; ns++)
    {
        disposeNamespace(ns);
    }
    ByteVectorDispose(&namespaceData);
    IntHashMapDispose(&fileNamespace);
}


namespaceref NamespaceCreate(fileref file, stringref name unused)
{
    Namespace *entry;
    namespaceref ref =
        refFromSize(ByteVectorSize(&namespaceData) / sizeof(Namespace));

    ByteVectorGrow(&namespaceData, sizeof(Namespace));
    entry = getNamespace(ref);
    IntHashMapInit(&entry->namespaces, 10);
    IntHashMapInit(&entry->fieldIndex, 10);
    IntHashMapInit(&entry->functionIndex, 10);
    IntHashMapInit(&entry->targetIndex, 10);
    IntHashMapAdd(&fileNamespace, uintFromRef(file), uintFromRef(ref));
    return ref;
}

namespaceref NamespaceGet(fileref file)
{
    return refFromUint(IntHashMapGet(&fileNamespace, uintFromRef(file)));
}


void NamespaceAddField(namespaceref ns, stringref name, fieldref field)
{
    IntHashMapAdd(&getNamespace(ns)->fieldIndex,
                  uintFromRef(name), uintFromRef(field));
}

void NamespaceAddFunction(namespaceref ns, stringref name, functionref function)
{
    IntHashMapAdd(&getNamespace(ns)->functionIndex,
                  uintFromRef(name), uintFromRef(function));
}

void NamespaceAddTarget(namespaceref ns, stringref name, functionref target)
{
    NamespaceAddFunction(ns, name, target);
    IntHashMapAdd(&getNamespace(ns)->targetIndex,
                  uintFromRef(name), uintFromRef(target));
}


fieldref NamespaceGetField(namespaceref ns, stringref name)
{
    uint i = IntHashMapGet(&getNamespace(ns)->fieldIndex, uintFromRef(name));
    if (!i)
    {
        i = IntHashMapGet(&getNamespace(0)->fieldIndex, uintFromRef(name));
    }
    return refFromUint(i);
}

functionref NamespaceGetFunction(namespaceref ns, stringref name)
{
    uint i = IntHashMapGet(&getNamespace(ns)->functionIndex, uintFromRef(name));
    if (!i)
    {
        i = IntHashMapGet(&getNamespace(0)->functionIndex, uintFromRef(name));
    }
    return refFromUint(i);
}

functionref NamespaceGetTarget(namespaceref ns, stringref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->targetIndex,
                                     uintFromRef(name)));
}
