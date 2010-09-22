extern ErrorCode ParserAddKeywords(void);
extern ErrorCode ParseFile(fileref file);
extern nonnull ErrorCode ParseFunction(functionref function,
                                       bytevector *bytecode);
