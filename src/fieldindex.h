extern void FieldIndexInit(void);
extern void FieldIndexDispose(void);

extern void FieldIndexFinishBytecode(const byte *parsed,
                                     bytevector *bytecode);

extern fieldref FieldIndexAdd(fileref file, uint line, uint fileOffset);
extern fieldref FieldIndexAddConstant(fileref file, uint line, uint fileOffset,
                                      bytevector *bytecode, size_t start);
extern void FieldIndexSetBytecodeOffset(fieldref field,
                                        size_t start, size_t stop);

extern pure uint FieldIndexGetCount(void);
extern pure fieldref FieldIndexGetFirstField(void);
extern pure fieldref FieldIndexGetNextField(fieldref field);
extern pureconst uint FieldIndexGetIndex(fieldref field);
extern pure fileref FieldIndexGetFile(fieldref field);
extern pure uint FieldIndexGetLine(fieldref field);
extern pure uint FieldIndexGetFileOffset(fieldref field);
