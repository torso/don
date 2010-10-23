extern void FieldIndexInit(void);
extern void FieldIndexDispose(void);

extern void FieldIndexFinishBytecode(const byte *parsed,
                                     bytevector *bytecode);

extern fieldref FieldIndexAdd(fileref file, uint line, uint fileOffset);
extern void FieldIndexSetBytecodeOffset(fieldref field,
                                        size_t start, size_t stop);

extern uint FieldIndexGetCount(void);
extern fieldref FieldIndexGetFirstField(void);
extern fieldref FieldIndexGetNextField(fieldref field);
extern uint FieldIndexGetIndex(fieldref field);
extern fileref FieldIndexGetFile(fieldref field);
extern uint FieldIndexGetLine(fieldref field);
extern uint FieldIndexGetFileOffset(fieldref field);
