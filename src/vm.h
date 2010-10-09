#include "bytevector.h"
#include "intvector.h"

struct VM
{
    const byte *restrict bytecode;

    ErrorCode error;

    uint *fields;
    intvector callStack;
    intvector stack;
    byte *heapBase;
    byte *heapFree;

    bytevector *pipeOut;
    bytevector *pipeErr;

    uint booleanTrue;
    uint booleanFalse;
    uint emptyString;
    uint emptyList;
};

#include "heap.h"
