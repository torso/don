extern ErrorCode FieldIndexInit(void);
extern void FieldIndexDispose(void);

extern ErrorCode FieldIndexFinishBytecode(const byte *parsed,
                                          bytevector *bytecode);

extern fieldref FieldIndexAdd(fileref file, uint line, uint fileOffset);
extern void FieldIndexSetBytecodeOffset(fieldref field, uint start, uint stop);

extern uint FieldIndexGetCount(void);
extern fieldref FieldIndexGetFirstField(void);
extern fieldref FieldIndexGetNextField(fieldref field);
extern uint FieldIndexGetIndex(fieldref field);
extern fileref FieldIndexGetFile(fieldref field);
extern uint FieldIndexGetLine(fieldref field);
extern uint FieldIndexGetFileOffset(fieldref field);
