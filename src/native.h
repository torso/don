#define NATIVE_MAX_VALUES 9

struct _Work;

extern void NativeInit(void);
extern nonnull void NativeInvoke(VM *vm, nativefunctionref function);
extern nonnull void NativeWork(struct _Work *work);
extern nativefunctionref NativeFindFunction(vref name);
extern vref NativeGetName(nativefunctionref function);
extern uint NativeGetParameterCount(nativefunctionref function);
extern uint NativeGetReturnValueCount(nativefunctionref function);
