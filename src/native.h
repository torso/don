#ifndef STRINGPOOL_H
#error stringpool.h not included
#endif

#define NATIVE_H

typedef int nativefunctionref;

extern void NativeInit(void);
extern pure nativefunctionref NativeFindFunction(stringref name);
extern pure uint NativeGetParameterCount(nativefunctionref function);
extern pure uint NativeGetMinimumArgumentCount(nativefunctionref function);
extern pure stringref *NativeGetParameterNames(nativefunctionref function);
