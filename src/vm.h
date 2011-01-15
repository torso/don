#include "bytevector.h"
#include "intvector.h"

struct VM
{
    objectref *fields;
    intvector callStack;
    intvector stack;
    byte *heapBase;
    byte *heapFree;
};

#include "heap.h"

extern const byte *vmBytecode;

/*
  The VM takes ownership of the bytecode.
*/
extern void VMInit(byte *bytecode);
extern void VMDispose(void);
