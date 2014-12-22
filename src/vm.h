#include "intvector.h"

struct _LinkedProgram;

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

extern nonnull VM *VMCreate(const struct _LinkedProgram *program);
extern nonnull VM *VMClone(VM *vm, vref condition, const int *ip);
extern nonnull void VMDispose(VM *vm);
extern nonnull void VMHalt(VM *vm, vref failMessage);
extern attrprintf(3, 4) void VMFail(VM *vm, const int *ip, const char *format, ...);

extern nonnull vref VMReadValue(VM *vm);
extern nonnull void VMStoreValue(VM *vm, int variable, vref value);
