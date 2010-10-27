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

    objectref booleanTrue;
    objectref booleanFalse;
    objectref emptyString;
    objectref emptyList;
    objectref stringNewline;
};

#include "heap.h"
