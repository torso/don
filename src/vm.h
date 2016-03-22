#include "intvector.h"

struct _Job;
struct _LinkedProgram;
struct VMBase;

typedef struct VMBase
{
    struct VMBase *parent;
    uint clonePoints;
    bool fullVM;
} VMBase;

struct VMBranch
{
    VMBase base;
    uint childCount;
    VMBase *children[2];
};

struct VM
{
    VMBase base;

    const vref *constants;
    int constantCount;
    vref *fields;
    int fieldCount;
    intvector callStack;
    intvector stack;

    const int *ip;
    int bp;
    bool idle;
    struct _Job *job;
    VMBase *child;
    vref failMessage;
};


extern const int *vmBytecode;
extern const int *vmLineNumbers;

nonnull VM *VMCreate(const struct _LinkedProgram *program);
nonnull VM *VMClone(VM *vmState, const int *ip);
nonnull void VMCloneBranch(VM *vmState, const int *ip);
nonnull void VMReplaceCloneBranch(VM *vmState, const int *ip);
nonnull void VMDispose(VMBase *base);
nonnull VMBase *VMDisposeBranch(VMBranch *branch, uint keepBranch);
nonnull void VMReplaceChild(VM *vm, VM *child);
nonnull void VMHalt(VM *vm, vref failMessage);
nonnull void VMFail(VM *vm, const char *msg, size_t msgSize);
attrprintf(2, 3) void VMFailf(VM *vm, const char *format, ...);

nonnull vref VMReadValue(VM *vmState);
nonnull void VMStoreValue(VM *vmState, int variable, vref value);
