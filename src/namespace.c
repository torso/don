#include "common.h"
#include "bytevector.h"
#include "inthashmap.h"
#include "namespace.h"

typedef struct
{
    stringref name;
    inthashmap namespaces;
    inthashmap fieldIndex;
    inthashmap functionIndex;
    inthashmap targetIndex;
} Namespace;

static bytevector namespaceData;
static inthashmap fileNamespace;
static inthashmap nameNamespace;


static Namespace *getNamespace(namespaceref ns)
{
    assert(ns);
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
    ByteVectorSetSize(&namespaceData, sizeof(Namespace));
    IntHashMapInit(&fileNamespace, 4);
    IntHashMapInit(&nameNamespace, 4);
}

void NamespaceDispose(void)
{
    Namespace *ns;
    Namespace *limit = (Namespace*)ByteVectorGetAppendPointer(&namespaceData);

    for (ns = getNamespace(1); ns < limit; ns++)
    {
        disposeNamespace(ns);
    }
    ByteVectorDispose(&namespaceData);
    IntHashMapDispose(&fileNamespace);
    IntHashMapDispose(&nameNamespace);
}


namespaceref NamespaceCreate(fileref file, stringref name)
{
    Namespace *entry;
    namespaceref ref =
        refFromSize(ByteVectorSize(&namespaceData) / sizeof(Namespace));

    ByteVectorGrow(&namespaceData, sizeof(Namespace));
    entry = getNamespace(ref);
    entry->name = name;
    IntHashMapInit(&entry->namespaces, 10);
    IntHashMapInit(&entry->fieldIndex, 10);
    IntHashMapInit(&entry->functionIndex, 10);
    IntHashMapInit(&entry->targetIndex, 10);
    IntHashMapAdd(&fileNamespace, uintFromRef(file), uintFromRef(ref));
    if (name)
    {
        IntHashMapAdd(&nameNamespace, uintFromRef(name), uintFromRef(ref));
    }
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


stringref NamespaceGetName(namespaceref ns)
{
    return getNamespace(ns)->name;
}

namespaceref NamespaceGetNamespace(namespaceref ns unused, stringref name)
{
    return IntHashMapGet(&nameNamespace, name);
}

fieldref NamespaceGetField(namespaceref ns, stringref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->fieldIndex,
                                     uintFromRef(name)));
}

functionref NamespaceGetFunction(namespaceref ns, stringref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->functionIndex,
                                     uintFromRef(name)));
}

functionref NamespaceGetTarget(namespaceref ns, stringref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->targetIndex,
                                     uintFromRef(name)));
}


fieldref NamespaceLookupField(namespaceref ns, stringref name)
{
    uint i = IntHashMapGet(&getNamespace(ns)->fieldIndex, uintFromRef(name));
    if (!i)
    {
        i = IntHashMapGet(&getNamespace(1)->fieldIndex, uintFromRef(name));
    }
    return refFromUint(i);
}

functionref NamespaceLookupFunction(namespaceref ns, stringref name)
{
    uint i = IntHashMapGet(&getNamespace(ns)->functionIndex, uintFromRef(name));
    if (!i)
    {
        i = IntHashMapGet(&getNamespace(1)->functionIndex, uintFromRef(name));
    }
    return refFromUint(i);
}
