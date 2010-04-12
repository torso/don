#ifndef BYTEVECTOR_H
#error bytevector.h not included
#endif
#ifndef STRINGPOOL_H
#error stringpool.h not included
#endif
#ifndef INTERPRETERSTATE_H
#error interpreterstate.h not included
#endif

#define NATIVE_H

typedef int nativefunctionref;

extern nonnull void NativeInvoke(RunState *state, nativefunctionref function);
extern pure nativefunctionref NativeFindFunction(stringref name);
extern pure uint NativeGetParameterCount(nativefunctionref function);
extern pure uint NativeGetMinimumArgumentCount(nativefunctionref function);
extern pure stringref *NativeGetParameterNames(nativefunctionref function);
extern pure uint NativeGetBytecodeOffset(nativefunctionref function);
extern void NativeWriteBytecode(bytevector *restrict bytecode,
                                bytevector *restrict valueBytecode);
