#include "bytevector.h"
#include "intvector.h"

struct _VMBranch;
typedef struct _VMBranch VMBranch;
struct _VMBranch
{
    VMBranch *parent;
    objectref condition;
    uint children;
    uint mutableCount;
    objectref mutableIndex[1];
};

struct VM
{
    VMBranch *parent;
    objectref condition;

    uint mutableCount;
    intvector mutableIndex;
    objectref *fields;
    intvector callStack;
    intvector stack;

    functionref target;
    const byte *ip;
    uint bp;
};

#include "instruction.h"
#include "heap.h"


extern nonnull VM *VMCreate(const byte *bytecode, functionref target);
extern nonnull VM *VMClone(VM *vm, objectref condition, const byte *ip);
extern nonnull void VMDispose(VM *vm);

extern nonnull uint VMAddMutable(VM *vm, objectref object);
extern nonnull objectref VMGetMutable(VM *vm, uint index);

extern nonnull objectref VMPeek(VM *vm);
extern nonnull objectref VMPop(VM *vm);
extern nonnull void VMPopMany(VM *vm, objectref *dst, uint count);
extern nonnull void VMPush(VM *vm, objectref value);
extern nonnull void VMPushBoolean(VM *vm, boolean value);
extern nonnull void VMPushMany(VM *vm, const objectref *values, uint count);
