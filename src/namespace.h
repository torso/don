extern void NamespaceInit(void);
extern void NamespaceDispose(void);

extern namespaceref NamespaceCreate(fileref file, stringref name);
extern namespaceref NamespaceGet(fileref file);

extern void NamespaceAddField(namespaceref ns, stringref name, fieldref field);
extern void NamespaceAddFunction(namespaceref ns, stringref name,
                                 functionref function);
extern void NamespaceAddTarget(namespaceref ns, stringref name,
                               functionref target);

extern stringref NamespaceGetName(namespaceref ns);
extern namespaceref NamespaceGetNamespace(namespaceref ns, stringref name);
extern fieldref NamespaceGetField(namespaceref ns, stringref name);
extern functionref NamespaceGetFunction(namespaceref ns, stringref name);
extern functionref NamespaceGetTarget(namespaceref ns, stringref name);

extern fieldref NamespaceLookupField(namespaceref ns, stringref name);
extern functionref NamespaceLookupFunction(namespaceref ns, stringref name);
