#define NAMESPACE_DON 1

extern void NamespaceInit(void);
extern void NamespaceDispose(void);

extern namespaceref NamespaceCreate(vref name);

extern void NamespaceAddField(namespaceref ns, vref name, fieldref field);
extern void NamespaceAddFunction(namespaceref ns, vref name,
                                 functionref function);
extern void NamespaceAddTarget(namespaceref ns, vref name,
                               functionref target);

extern vref NamespaceGetName(namespaceref ns);
extern namespaceref NamespaceGetNamespace(namespaceref ns, vref name);
extern fieldref NamespaceGetField(namespaceref ns, vref name);
extern functionref NamespaceGetFunction(namespaceref ns, vref name);
extern functionref NamespaceGetTarget(namespaceref ns, vref name);

extern fieldref NamespaceLookupField(namespaceref ns, vref name);
extern functionref NamespaceLookupFunction(namespaceref ns, vref name);
