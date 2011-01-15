#include "bytevector.h"
#include "intvector.h"

struct VM
{
    const byte *restrict bytecode;

    objectref *fields;
    intvector callStack;
    intvector stack;
    byte *heapBase;
    byte *heapFree;
};

extern objectref vmTrue;
extern objectref vmFalse;
extern objectref vmEmptyString;
extern objectref vmEmptyList;
extern objectref vmNewline;

#include "heap.h"
