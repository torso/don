typedef enum
{
    TYPE_NULL_LITERAL,
    TYPE_BOOLEAN_LITERAL,
    TYPE_INTEGER_LITERAL,
    TYPE_STRING_LITERAL,
    TYPE_OBJECT
} ValueType;

/*
  Converts the value to a string of the default form. The returned memory must
  be freed with InterpreterFreeStringBuffer.
*/
extern nonnull const char *InterpreterGetString(RunState *state,
                                                ValueType type, uint value);
extern nonnull void InterpreterFreeStringBuffer(RunState *state,
                                                const char *buffer);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern nonnull size_t InterpreterGetStringSize(RunState *state,
                                               ValueType type, uint value);
extern nonnull void InterpreterCopyString(RunState *restrict state,
                                          ValueType type, uint value,
                                          byte *restrict dst);

extern nonnull ValueType InterpreterPeekType(RunState *state);
extern nonnull void InterpreterPop(RunState *state,
                                   ValueType *type, uint *value);
extern nonnull boolean InterpreterPush(RunState *state,
                                       ValueType type, uint value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
