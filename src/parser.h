extern void ParserAddKeywords(void);
extern void ParseFile(vref filename, namespaceref ns);
extern nonnull void ParseField(fieldref field, bytevector *bytecode);
extern nonnull void ParseFunctionDeclaration(functionref function,
                                             bytevector *bytecode);
extern nonnull void ParseFunctionBody(functionref function,
                                      bytevector *bytecode);
