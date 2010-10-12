extern ErrorCode ParserAddKeywords(void);
extern ErrorCode ParseFile(fileref file);
extern nonnull ErrorCode ParseField(fieldref field, bytevector *bytecode);
extern nonnull ErrorCode ParseFunctionDeclaration(functionref function,
                                                  bytevector *bytecode);
extern nonnull ErrorCode ParseFunctionBody(functionref function,
                                           bytevector *bytecode);
