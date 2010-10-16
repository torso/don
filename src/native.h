extern ErrorCode NativeInit(bytevector *bytecode);
extern nonnull void NativeInvoke(VM *vm, nativefunctionref function,
                                 uint returnValues);
extern nativefunctionref NativeFindFunction(stringref name);
extern stringref NativeGetName(nativefunctionref function);
extern uint NativeGetParameterCount(nativefunctionref function);
extern const ParameterInfo *NativeGetParameterInfo(nativefunctionref function);
extern boolean NativeHasVararg(nativefunctionref function);
extern uint NativeGetVarargIndex(nativefunctionref function);
