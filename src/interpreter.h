extern nonnull bytevector *InterpreterGetPipeOut(VM *vm);
extern nonnull bytevector *InterpreterGetPipeErr(VM *vm);

/*
  Converts the value to a string of the default form. The returned memory must
  be freed with InterpreterFreeStringBuffer.
*/
extern nonnull const char *InterpreterGetString(VM *vm, uint value);
extern nonnull void InterpreterFreeStringBuffer(VM *vm, const char *buffer);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern nonnull size_t InterpreterGetStringSize(VM *vm, uint value);
extern nonnull byte *InterpreterCopyString(VM *restrict vm, uint value,
                                           byte *restrict dst);

extern nonnull ErrorCode InterpreterCreateString(VM *vm,
                                                 const char *string,
                                                 size_t length,
                                                 uint *value);

extern nonnull uint InterpreterPeek(VM *vm);
extern nonnull uint InterpreterPop(VM *vm);
extern nonnull boolean InterpreterPush(VM *vm, uint value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
