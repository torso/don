extern ErrorCode NativeInit(void);
extern nonnull ErrorCode NativeInvoke(VM *vm, nativefunctionref function,
                                      uint returnValues);
extern nativefunctionref NativeFindFunction(stringref name);
extern stringref NativeGetName(nativefunctionref function);
extern uint NativeGetParameterCount(nativefunctionref function);
extern uint NativeGetMinimumArgumentCount(nativefunctionref function);
extern const ParameterInfo *NativeGetParameterInfo(nativefunctionref function);
