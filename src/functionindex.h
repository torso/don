extern void FunctionIndexInit(void);
extern void FunctionIndexDispose(void);

extern functionref FunctionIndexAddFunction(namespaceref ns, stringref name,
                                            stringref filename, uint line,
                                            uint fileOffset);
extern void FunctionIndexAddParameter(functionref function, stringref name,
                                      boolean hasValue, objectref value,
                                      boolean vararg);
extern void FunctionIndexFinishParameters(functionref function,
                                          uint line, uint fileOffset);
extern void FunctionIndexSetFailedDeclaration(functionref function);

extern functionref FunctionIndexGetFirstFunction(void);
extern functionref FunctionIndexGetNextFunction(functionref function);

extern functionref FunctionIndexGetFunctionFromBytecode(uint bytecodeOffset);
extern stringref FunctionIndexGetName(functionref function);
extern namespaceref FunctionIndexGetNamespace(functionref function);
extern stringref FunctionIndexGetFilename(functionref function);
extern uint FunctionIndexGetLine(functionref function);
extern uint FunctionIndexGetFileOffset(functionref function);
extern boolean FunctionIndexIsTarget(functionref function);
extern uint FunctionIndexGetBytecodeOffset(functionref function);
extern void FunctionIndexSetBytecodeOffset(functionref function, size_t offset);
extern uint FunctionIndexGetParameterCount(functionref function);
extern uint FunctionIndexGetRequiredArgumentCount(functionref function);
extern const ParameterInfo *FunctionIndexGetParameterInfo(functionref function);
extern boolean FunctionIndexHasVararg(functionref function);
extern uint FunctionIndexGetVarargIndex(functionref function);
extern uint FunctionIndexGetLocalsCount(functionref function);
extern stringref FunctionIndexGetLocalName(functionref function,
                                           uint16 local);
extern nonnull void FunctionIndexSetLocals(functionref function,
                                           const inthashmap *locals,
                                           uint count);
