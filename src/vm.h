#include "bytevector.h"
#include "intvector.h"

struct _VMBranch;
typedef struct _VMBranch VMBranch;

struct _VMBranch
{
    VMBranch *parent;
    vref condition;
    uint childCount;
};

struct VM
{
    VMBranch *parent;
    vref condition;

    vref *fields;
    intvector callStack;
    intvector stack;

    functionref target;
    const byte *ip;
    uint bp;
};

#include "heap.h"


extern nonnull VM *VMCreate(const byte *bytecode, functionref target);
extern nonnull VM *VMClone(VM *vm, vref condition, const byte *ip);
extern nonnull void VMDispose(VM *vm);

extern nonnull vref VMReadValue(VM *vm);
