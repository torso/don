#define NAMESPACE_DON 1

extern void NamespaceInit(void);
extern void NamespaceDispose(void);

extern namespaceref NamespaceCreate(objectref name);

extern void NamespaceAddField(namespaceref ns, objectref name, fieldref field);
extern void NamespaceAddFunction(namespaceref ns, objectref name,
                                 functionref function);
extern void NamespaceAddTarget(namespaceref ns, objectref name,
                               functionref target);

extern objectref NamespaceGetName(namespaceref ns);
extern namespaceref NamespaceGetNamespace(namespaceref ns, objectref name);
extern fieldref NamespaceGetField(namespaceref ns, objectref name);
extern functionref NamespaceGetFunction(namespaceref ns, objectref name);
extern functionref NamespaceGetTarget(namespaceref ns, objectref name);

extern fieldref NamespaceLookupField(namespaceref ns, objectref name);
extern functionref NamespaceLookupFunction(namespaceref ns, objectref name);
