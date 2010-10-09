extern ErrorCode NativeInit(void);
extern nonnull ErrorCode NativeInvoke(VM *vm, nativefunctionref function,
                                      uint returnValues);
extern pure nativefunctionref NativeFindFunction(stringref name);
extern pure stringref NativeGetName(nativefunctionref function);
extern pure uint NativeGetParameterCount(nativefunctionref function);
extern pure uint NativeGetMinimumArgumentCount(nativefunctionref function);
extern pure const stringref *NativeGetParameterNames(nativefunctionref function);
