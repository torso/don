#define NAMESPACE_DON 1

extern void NamespaceInit(void);
extern void NamespaceDispose(void);

extern namespaceref NamespaceCreate(vref name);

extern int NamespaceAddField(namespaceref ns, vref name, int index);
extern int NamespaceAddFunction(namespaceref ns, vref name, int index);
extern int NamespaceAddTarget(namespaceref ns, vref name, int index);

extern vref NamespaceGetName(namespaceref ns);
extern namespaceref NamespaceGetNamespace(namespaceref ns, vref name);
extern int NamespaceGetField(namespaceref ns, vref name);
extern int NamespaceGetFunction(namespaceref ns, vref name);
extern int NamespaceGetTarget(namespaceref ns, vref name);

extern int NamespaceLookupField(namespaceref ns, vref name);
extern int NamespaceLookupFunction(namespaceref ns, vref name);
