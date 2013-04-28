extern void FieldIndexInit(void);
extern void FieldIndexDispose(void);

extern void FieldIndexFinishBytecode(const byte *parsed,
                                     bytevector *bytecode);

extern fieldref FieldIndexAdd(namespaceref ns,
                              objectref filename, uint line, uint fileOffset);
extern void FieldIndexSetBytecodeOffset(fieldref field,
                                        size_t start, size_t stop);

extern pure size_t FieldIndexGetCount(void);
extern pure fieldref FieldIndexGetFirstField(void);
extern pure fieldref FieldIndexGetNextField(fieldref field);
extern pureconst uint FieldIndexGetIndex(fieldref field);
extern pureconst fieldref FieldIndexFromIndex(uint index);
extern pure namespaceref FieldIndexGetNamespace(fieldref field);
extern pure objectref FieldIndexGetFilename(fieldref field);
extern pure uint FieldIndexGetLine(fieldref field);
extern pure uint FieldIndexGetFileOffset(fieldref field);
