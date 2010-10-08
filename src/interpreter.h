extern nonnull Heap *InterpreterGetHeap(RunState *state);
extern nonnull bytevector *InterpreterGetPipeOut(RunState *state);
extern nonnull bytevector *InterpreterGetPipeErr(RunState *state);

/*
  Converts the value to a string of the default form. The returned memory must
  be freed with InterpreterFreeStringBuffer.
*/
extern nonnull const char *InterpreterGetString(RunState *state, uint value);
extern nonnull void InterpreterFreeStringBuffer(RunState *state,
                                                const char *buffer);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern nonnull size_t InterpreterGetStringSize(RunState *state, uint value);
extern nonnull byte *InterpreterCopyString(RunState *restrict state, uint value,
                                           byte *restrict dst);

extern nonnull ErrorCode InterpreterCreateString(RunState *state,
                                                 const char *string,
                                                 size_t length,
                                                 uint *value);

extern nonnull uint InterpreterPeek(RunState *state);
extern nonnull uint InterpreterPop(RunState *state);
extern nonnull boolean InterpreterPush(RunState *state, uint value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
