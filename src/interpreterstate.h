#ifndef BYTEVECTOR_H
#error bytevector.h not included
#endif
#ifndef INTVECTOR_H
#error intvector.h not included
#endif

#define INTERPRETERSTATE_H

typedef struct
{
    const bytevector *restrict bytecode;
    const bytevector *restrict valueBytecode;

    uint ip;
    uint bp;
    ErrorCode error;
    intvector values;
    intvector stack;
    bytevector heap;
} RunState;