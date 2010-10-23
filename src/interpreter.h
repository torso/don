extern nonnull objectref InterpreterPeek(VM *vm);
extern nonnull objectref InterpreterPop(VM *vm);
extern nonnull boolean InterpreterPopBoolean(VM *vm);
extern nonnull void InterpreterPush(VM *vm, objectref value);

extern nonnull void InterpreterInit(VM *vm, const byte *bytecode);
extern nonnull void InterpreterDispose(VM *vm);
extern nonnull void InterpreterExecute(VM *vm, functionref target);
