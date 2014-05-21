#define NATIVE_MAX_VALUES 10

struct _Work;

extern void NativeInit(void);
extern nonnull vref NativeInvoke(VM *vm, nativefunctionref function);
extern nativefunctionref NativeFindFunction(vref name);
extern vref NativeGetName(nativefunctionref function);
extern uint NativeGetParameterCount(nativefunctionref function);
extern uint NativeGetReturnValueCount(nativefunctionref function);
