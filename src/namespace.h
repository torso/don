extern void NamespaceInit(void);
extern void NamespaceDispose(void);

extern void NamespaceAddField(stringref name, fieldref field);
extern void NamespaceAddFunction(stringref name, functionref function);
extern void NamespaceAddTarget(stringref name, functionref target);

extern fieldref NamespaceGetField(stringref name);
extern functionref NamespaceGetFunction(stringref name);
extern functionref NamespaceGetTarget(stringref name);
