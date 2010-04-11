#ifndef BYTEVECTOR_H
#error bytevector.h not included
#endif
#ifndef INTVECTOR_H
#error intvector.h not included
#endif

typedef struct
{
    const bytevector *restrict bytecode;
    const bytevector *restrict valueBytecode;

    uint ip;
    uint bp;
    intvector values;
    intvector stack;
    bytevector heap;
} RunState;
