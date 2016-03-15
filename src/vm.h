#include "intvector.h"

struct _LinkedProgram;

struct VMBranch
{
    VMBranch *parent;

    /* When true, this VM branch will actually run. */
    vref condition;

    uint childCount;
    void **children;

    /* When true, children is an array of VM*.
       When false, children in an array of VMBranch*. */
    bool leaf;
};

struct VM
{
    VMBranch *branch;

    const vref *constants;
    int constantCount;
    vref *fields;
    int fieldCount;
    intvector callStack;
    intvector stack;

    const int *ip;
    int bp;
    bool active;
    vref failMessage;
};


extern const int *vmBytecode;
extern const int *vmLineNumbers;

nonnull VM *VMCreate(const struct _LinkedProgram *program);
nonnull VM *VMClone(VM *vmState, vref condition, const int *ip);
nonnull void VMDispose(VM *vm);
nonnull void VMHalt(VM *vm, vref failMessage);
attrprintf(3, 4) void VMFail(VM *vm, const int *ip, const char *format, ...);
void VMBranchFail(VMBranch *branch, const int *ip, vref failMessage);
attrprintf(3, 4) void VMBranchFailf(VMBranch *branch, const int *ip, const char *format, ...);

nonnull vref VMReadValue(VM *vmState);
nonnull void VMStoreValue(VM *vmState, int variable, vref value);
