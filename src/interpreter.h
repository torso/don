extern nonnull objectref InterpreterPeek(VM *vm);
extern nonnull objectref InterpreterPop(VM *vm);
extern nonnull boolean InterpreterPopBoolean(VM *vm);
extern nonnull void InterpreterPush(VM *vm, objectref value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
