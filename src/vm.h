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
