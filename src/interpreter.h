extern nonnull bytevector *InterpreterGetPipeOut(VM *vm);
extern nonnull bytevector *InterpreterGetPipeErr(VM *vm);

/*
  Converts the value to a string of the default form. The returned memory must
  be freed with InterpreterFreeStringBuffer.
*/
extern nonnull const char *InterpreterGetString(VM *vm, objectref value);
extern nonnull void InterpreterFreeStringBuffer(VM *vm, const char *buffer);

extern nonnull objectref InterpreterPeek(VM *vm);
extern nonnull objectref InterpreterPop(VM *vm);
extern nonnull boolean InterpreterPush(VM *vm, objectref value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
