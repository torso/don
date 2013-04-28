#include "common.h"
#include "bytevector.h"
#include "inthashmap.h"
#include "namespace.h"

typedef struct
{
    objectref name;
    inthashmap namespaces;
    inthashmap fieldIndex;
    inthashmap functionIndex;
    inthashmap targetIndex;
} Namespace;

static boolean initialised;
static bytevector namespaceData;
static inthashmap nameNamespace;


static Namespace *getNamespace(namespaceref ns)
{
    assert(ns);
    return (Namespace*)BVGetPointer(
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
    BVInit(&namespaceData, sizeof(Namespace) * 4);
    BVSetSize(&namespaceData, sizeof(Namespace));
    IntHashMapInit(&nameNamespace, 4);
    initialised = true;
}

void NamespaceDispose(void)
{
    Namespace *ns;
    Namespace *limit;

    if (initialised)
    {
        limit = (Namespace*)BVGetAppendPointer(&namespaceData);
        for (ns = getNamespace(1); ns < limit; ns++)
        {
            disposeNamespace(ns);
        }
        BVDispose(&namespaceData);
        IntHashMapDispose(&nameNamespace);
    }
}


namespaceref NamespaceCreate(objectref name)
{
    Namespace *entry;
    namespaceref ref =
        refFromSize(BVSize(&namespaceData) / sizeof(Namespace));

    BVGrow(&namespaceData, sizeof(Namespace));
    entry = getNamespace(ref);
    entry->name = name;
    IntHashMapInit(&entry->namespaces, 1);
    IntHashMapInit(&entry->fieldIndex, 32);
    IntHashMapInit(&entry->functionIndex, 32);
    IntHashMapInit(&entry->targetIndex, 32);
    if (name)
    {
        IntHashMapAdd(&nameNamespace, uintFromRef(name), uintFromRef(ref));
    }
    return ref;
}


void NamespaceAddField(namespaceref ns, objectref name, fieldref field)
{
    IntHashMapAdd(&getNamespace(ns)->fieldIndex,
                  uintFromRef(name), uintFromRef(field));
}

void NamespaceAddFunction(namespaceref ns, objectref name, functionref function)
{
    IntHashMapAdd(&getNamespace(ns)->functionIndex,
                  uintFromRef(name), uintFromRef(function));
}

void NamespaceAddTarget(namespaceref ns, objectref name, functionref target)
{
    NamespaceAddFunction(ns, name, target);
    IntHashMapAdd(&getNamespace(ns)->targetIndex,
                  uintFromRef(name), uintFromRef(target));
}


objectref NamespaceGetName(namespaceref ns)
{
    return getNamespace(ns)->name;
}

namespaceref NamespaceGetNamespace(namespaceref ns unused, objectref name)
{
    return refFromUint(IntHashMapGet(&nameNamespace, uintFromRef(name)));
}

fieldref NamespaceGetField(namespaceref ns, objectref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->fieldIndex,
                                     uintFromRef(name)));
}

functionref NamespaceGetFunction(namespaceref ns, objectref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->functionIndex,
                                     uintFromRef(name)));
}

functionref NamespaceGetTarget(namespaceref ns, objectref name)
{
    return refFromUint(IntHashMapGet(&getNamespace(ns)->targetIndex,
                                     uintFromRef(name)));
}


fieldref NamespaceLookupField(namespaceref ns, objectref name)
{
    uint i = IntHashMapGet(&getNamespace(ns)->fieldIndex, uintFromRef(name));
    if (!i)
    {
        i = IntHashMapGet(&getNamespace(NAMESPACE_DON)->fieldIndex, uintFromRef(name));
    }
    return refFromUint(i);
}

functionref NamespaceLookupFunction(namespaceref ns, objectref name)
{
    uint i = IntHashMapGet(&getNamespace(ns)->functionIndex, uintFromRef(name));
    if (!i)
    {
        i = IntHashMapGet(&getNamespace(NAMESPACE_DON)->functionIndex, uintFromRef(name));
    }
    return refFromUint(i);
}
