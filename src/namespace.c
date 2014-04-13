#include "common.h"
#include "bytevector.h"
#include "inthashmap.h"
#include "namespace.h"

typedef struct
{
    vref name;
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


namespaceref NamespaceCreate(vref name)
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
        IntHashMapAdd(&nameNamespace, intFromRef(name), intFromRef(ref));
    }
    return ref;
}


int NamespaceAddField(namespaceref ns, vref name, int index)
{
    return IntHashMapSet(&getNamespace(ns)->fieldIndex, intFromRef(name), index + 1) - 1;
}

int NamespaceAddFunction(namespaceref ns, vref name, int index)
{
    return IntHashMapSet(&getNamespace(ns)->functionIndex, intFromRef(name), index + 1) - 1;
}

int NamespaceAddTarget(namespaceref ns, vref name, int index)
{
    int existingFunction = NamespaceAddFunction(ns, name, index);
    if (existingFunction >= 0)
    {
        return existingFunction;
    }
    existingFunction = IntHashMapSet(&getNamespace(ns)->targetIndex, intFromRef(name), index + 1);
    assert(!existingFunction);
    return -1;
}


vref NamespaceGetName(namespaceref ns)
{
    return getNamespace(ns)->name;
}

namespaceref NamespaceGetNamespace(namespaceref ns unused, vref name)
{
    return refFromInt(IntHashMapGet(&nameNamespace, intFromRef(name)));
}

int NamespaceGetField(namespaceref ns, vref name)
{
    return IntHashMapGet(&getNamespace(ns)->fieldIndex, intFromRef(name)) - 1;
}

int NamespaceGetFunction(namespaceref ns, vref name)
{
    return IntHashMapGet(&getNamespace(ns)->functionIndex, intFromRef(name)) - 1;
}

int NamespaceGetTarget(namespaceref ns, vref name)
{
    return IntHashMapGet(&getNamespace(ns)->targetIndex, intFromRef(name)) - 1;
}


int NamespaceLookupField(namespaceref ns, vref name)
{
    int i = NamespaceGetField(ns, name);
    if (i >= 0)
    {
        return i;
    }
    return NamespaceGetField(NAMESPACE_DON, name);
}

int NamespaceLookupFunction(namespaceref ns, vref name)
{
    int i = NamespaceGetFunction(ns, name);
    if (i >= 0)
    {
        return i;
    }
    return NamespaceGetFunction(NAMESPACE_DON, name);
}
