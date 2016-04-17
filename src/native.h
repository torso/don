#define NATIVE_MAX_VALUES 11

struct _Work;

void NativeInit(void);
nonnull vref NativeInvoke(VM *vm, nativefunctionref function);
nativefunctionref NativeFindFunction(vref name);
vref NativeGetName(nativefunctionref function);
uint NativeGetParameterCount(nativefunctionref function);
uint NativeGetReturnValueCount(nativefunctionref function);
