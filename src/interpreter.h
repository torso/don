typedef enum
{
    TYPE_NULL_LITERAL,
    TYPE_BOOLEAN_LITERAL,
    TYPE_INTEGER_LITERAL,
    TYPE_STRING_LITERAL
} ValueType;

extern nonnull ValueType InterpreterPeekType(RunState *state);
extern nonnull void InterpreterPop(RunState *state,
                                   ValueType *type, uint *value);
extern nonnull boolean InterpreterPush(RunState *state,
                                       ValueType type, uint value);

extern nonnull ErrorCode InterpreterExecute(const byte *bytecode,
                                            functionref target);
