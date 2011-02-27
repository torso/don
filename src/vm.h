#include "bytevector.h"
#include "intvector.h"

struct VM
{
    objectref *fields;
    intvector callStack;
    intvector stack;
};

#include "heap.h"

extern const byte *vmBytecode;


/*
  The VM will create and dispose the heap.
  The VM takes ownership of the bytecode.
*/
extern nonnull void VMInit(byte *bytecode);
extern void VMDispose(void);

extern nonnull objectref VMPeek(VM *vm);
extern nonnull objectref VMPop(VM *vm);
extern nonnull boolean VMPopBoolean(VM *vm);
extern nonnull void VMPush(VM *vm, objectref value);
extern nonnull void VMPushBoolean(VM *vm, boolean value);
