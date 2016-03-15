#define NAMESPACE_DON 1

void NamespaceInit(void);
void NamespaceDispose(void);

namespaceref NamespaceCreate(vref name);

int NamespaceAddField(namespaceref ns, vref name, int index);
int NamespaceAddFunction(namespaceref ns, vref name, int index);
int NamespaceAddTarget(namespaceref ns, vref name, int index);

vref NamespaceGetName(namespaceref ns);
namespaceref NamespaceGetNamespace(namespaceref ns, vref name);
int NamespaceGetField(namespaceref ns, vref name);
int NamespaceGetFunction(namespaceref ns, vref name);
int NamespaceGetTarget(namespaceref ns, vref name);

int NamespaceLookupField(namespaceref ns, vref name);
int NamespaceLookupFunction(namespaceref ns, vref name);
