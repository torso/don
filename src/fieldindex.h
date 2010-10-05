extern ErrorCode FieldIndexInit(void);
extern void FieldIndexDispose(void);

extern ErrorCode FieldIndexFinishBytecode(const byte *parsed,
                                          bytevector *bytecode);

extern fieldref FieldIndexAdd(fileref file, uint line, uint fileOffset);
extern void FieldIndexSetBytecodeOffset(fieldref field, uint start, uint stop);

extern uint FieldIndexGetCount(void);
extern fieldref FieldIndexGetFirstField(void);
extern fieldref FieldIndexGetNextField(fieldref field);
extern pure uint FieldIndexGetIndex(fieldref field);
extern pure fileref FieldIndexGetFile(fieldref field);
extern pure uint FieldIndexGetLine(fieldref field);
extern pure uint FieldIndexGetFileOffset(fieldref field);
