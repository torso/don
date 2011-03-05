struct _Work;

extern void NativeInit(void);
extern nonnull void NativeInvoke(VM *vm, nativefunctionref function);
extern nonnull void NativeWork(const struct _Work *work);
extern nativefunctionref NativeFindFunction(stringref name);
extern stringref NativeGetName(nativefunctionref function);
extern uint NativeGetParameterCount(nativefunctionref function);
extern uint NativeGetReturnValueCount(nativefunctionref function);
