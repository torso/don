#include "bytevector.h"
#include "intvector.h"

typedef ref_t objectref;

struct VM
{
    const byte *restrict bytecode;

    ErrorCode error;

    objectref *fields;
    intvector callStack;
    intvector stack;
    byte *heapBase;
    byte *heapFree;

    bytevector *pipeOut;
    bytevector *pipeErr;

    objectref booleanTrue;
    objectref booleanFalse;
    objectref emptyString;
    objectref emptyList;
};

#include "heap.h"
