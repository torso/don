extern ErrorCode NamespaceInit(void);
extern void NamespaceDispose(void);

extern ErrorCode NamespaceAddField(stringref name, fieldref field);
extern ErrorCode NamespaceAddFunction(stringref name, functionref function);
extern ErrorCode NamespaceAddTarget(stringref name, functionref target);

extern pure fieldref NamespaceGetField(stringref name);
extern pure functionref NamespaceGetFunction(stringref name);
extern pure functionref NamespaceGetTarget(stringref name);
