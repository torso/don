extern ErrorCode ParserAddKeywords(void);
extern ErrorCode ParseFile(fileref file);
extern nonnull ErrorCode ParseField(fieldref field, bytevector *bytecode);
extern nonnull ErrorCode ParseFunction(functionref function,
                                       bytevector *bytecode);
