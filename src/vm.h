#include "bytevector.h"
#include "intvector.h"

struct VM
{
    const byte *restrict bytecode;

    ErrorCode error;

    objectref *fields;
    intvector callStack;
    intvector stack;
    byte *heapBase;
    byte *heapFree;

    objectref booleanTrue;
    objectref booleanFalse;
    objectref emptyString;
    objectref emptyList;
};

#include "heap.h"
