extern nonnull bytevector *InterpreterGetPipeOut(VM *vm);
extern nonnull bytevector *InterpreterGetPipeErr(VM *vm);

/*
  Converts the value to a string of the default form. The returned memory must
  be freed with InterpreterFreeStringBuffer.
*/
extern nonnull const char *InterpreterGetString(VM *vm, objectref value);
extern nonnull void InterpreterFreeStringBuffer(VM *vm, const char *buffer);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern nonnull size_t InterpreterGetStringSize(VM *vm, objectref value);
extern nonnull byte *InterpreterCopyString(VM *restrict vm, objectref value,
                                           byte *restrict dst);

extern nonnull objectref InterpreterPeek(VM *vm);
extern nonnull objectref InterpreterPop(VM *vm);
extern nonnull boolean InterpreterPush(VM *vm, objectref value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
