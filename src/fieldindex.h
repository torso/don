enum
{
    FIELD_NULL,
    FIELD_TRUE,
    FIELD_FALSE,
    FIELD_EMPTY_LIST,

    RESERVED_FIELD_COUNT
};

extern void FieldIndexInit(void);
extern void FieldIndexDispose(void);

extern void FieldIndexFinishBytecode(const byte *parsed,
                                     bytevector *bytecode);

extern fieldref FieldIndexAdd(namespaceref ns,
                              stringref filename, uint line, uint fileOffset);
extern fieldref FieldIndexAddConstant(namespaceref ns, stringref filename,
                                      uint line, uint fileOffset,
                                      bytevector *bytecode, size_t start);
extern fieldref FieldIndexAddStringConstant(stringref string);
extern fieldref FieldIndexAddFileConstant(stringref string);
extern nonnull fieldref FieldIndexAddListConstant(const intvector *values);
extern void FieldIndexSetBytecodeOffset(fieldref field,
                                        size_t start, size_t stop);

extern pure size_t FieldIndexGetCount(void);
extern pure boolean FieldIndexIsConstant(fieldref field);
extern pure objectref FieldIndexValue(fieldref field);
extern nonnull void FieldIndexCopyValues(objectref *target);
extern pure fieldref FieldIndexGetFirstField(void);
extern pure fieldref FieldIndexGetNextField(fieldref field);
extern pureconst uint FieldIndexGetIndex(fieldref field);
extern pureconst fieldref FieldIndexFromIndex(uint index);
extern pure namespaceref FieldIndexGetNamespace(fieldref field);
extern pure stringref FieldIndexGetFilename(fieldref field);
extern pure uint FieldIndexGetLine(fieldref field);
extern pure uint FieldIndexGetFileOffset(fieldref field);
