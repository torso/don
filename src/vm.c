#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "bytecode.h"
#include "debug.h"
#include "linker.h"
#include "heap.h"
#include "instruction.h"
#include "job.h"
#include "vm.h"

const int *vmBytecode;
const int *vmLineNumbers;

static VM *VMAlloc(int fieldCount)
{
    VM *vm = (VM*)calloc(sizeof(VM) + (uint)fieldCount * sizeof(vref), 1);
    vm->base.fullVM = true;
    vm->fields = (vref*)(vm + 1);
    vm->fieldCount = fieldCount;
    IVInit(&vm->callStack, 128);
    IVInit(&vm->stack, 1024);
    return vm;
}

VM *VMCreate(const LinkedProgram *program)
{
    VM *vm = VMAlloc(program->fieldCount);
    if (DEBUG_VM)
    {
        printf("Created VM:%p\n", (void*)vm);
    }
    vm->constants = program->constants;
    vm->constantCount = program->constantCount;
    vm->bp = 0;
    memcpy(vm->fields, program->fields, (uint)vm->fieldCount * sizeof(*vm->fields));
    return vm;
}

static void VMCloneInit(const VM *vm, VM *clone, const int *ip)
{
    clone->constants = vm->constants;
    clone->constantCount = vm->constantCount;
    memcpy(clone->fields, vm->fields, (uint)vm->fieldCount * sizeof(*vm->fields));
    clone->fieldCount = vm->fieldCount;
    IVAppendAll(&vm->callStack, &clone->callStack);
    IVAppendAll(&vm->stack, &clone->stack);
    clone->ip = ip;
    clone->bp = vm->bp;
    clone->base.clonePoints = vm->base.clonePoints;
}

VM *VMClone(VM *vm, const int *ip)
{
    VM *clone = VMAlloc(vm->fieldCount);

    if (DEBUG_VM)
    {
        printf("Clone VM:%p clone:%p\n", (void*)vm, (void*)clone);
    }
    if (vm->child)
    {
        clone->child = vm->child;
        clone->child->parent = &clone->base;
    }
    vm->child = &clone->base;
    clone->base.parent = &vm->base;

    VMCloneInit(vm, clone, ip);
    return clone;
}

void VMCloneBranch(VM *vm, const int *ip)
{
    VMBranch *branch = (VMBranch*)calloc(sizeof(VMBranch), 1);
    VM *clone = VMAlloc(vm->fieldCount);
    VMBase *parent = vm->base.parent;

    if (DEBUG_VM)
    {
        printf("Clone VM:%p clone:%p branch:%p\n", (void*)vm, (void*)clone, (void*)branch);
    }

    assert(parent);
    assert(!vm->child);
    if (parent->fullVM)
    {
        VM *vmParent = (VM*)parent;
        assert(vmParent->child == &vm->base);
        vmParent->child = &branch->base;
    }
    else
    {
        VMBranch *vmParent = (VMBranch*)parent;
        VMBase **children = vmParent->children;
        while (*children != &vm->base)
        {
            children++;
            assert(children < vmParent->children + vmParent->childCount);
        }
        *children = &branch->base;
    }
    branch->base.parent = vm->base.parent;
    branch->childCount = 2;
    branch->children[0] = &vm->base;
    branch->children[1] = &clone->base;
    branch->base.clonePoints = vm->base.clonePoints;
    vm->base.parent = &branch->base;
    clone->base.parent = &branch->base;

    VMCloneInit(vm, clone, ip);
}

void VMReplaceCloneBranch(VM *vm, const int *ip)
{
    VMBranch *branch = (VMBranch*)vm->child;
    VM *clone = VMAlloc(vm->fieldCount);

    if (DEBUG_VM)
    {
        printf("Clone VM:%p clone:%p branch:%p\n", (void*)vm, (void*)clone, (void*)branch);
    }

    assert(branch);
    assert(!branch->base.fullVM);
    assert(branch->childCount == 2);
    assert(vm->base.parent);
    if (vm->base.parent->fullVM)
    {
        VM *parent = (VM*)vm->base.parent;
        parent->child = &branch->base;
    }
    else
    {
        VMBranch *parent = (VMBranch*)vm->base.parent;
        VMBase **children = parent->children;
        while (*children != &vm->base)
        {
            children++;
            assert(children < parent->children + parent->childCount);
        }
        *children = &branch->base;
    }
    branch->base.parent = vm->base.parent;
    vm->child = branch->children[0];
    clone->child = branch->children[1];
    vm->base.parent = &branch->base;
    clone->base.parent = &branch->base;
    branch->children[0]->parent = &vm->base;
    branch->children[1]->parent = &clone->base;
    branch->children[0] = &vm->base;
    branch->children[1] = &clone->base;

    VMCloneInit(vm, clone, ip);
}

void VMDispose(VMBase *base)
{
    if (DEBUG_VM)
    {
        printf("Dispose VM:%p\n", (void*)base);
    }
    if (base->fullVM)
    {
        VM *vm = (VM*)base;
        if (vm->job)
        {
            JobDiscard(vm->job);
        }
        base = vm->child;
        IVDispose(&vm->callStack);
        IVDispose(&vm->stack);
        free(vm);
        if (base)
        {
            VMDispose(base);
        }
    }
    else
    {
        VMBranch *branch = (VMBranch*)base;
        uint i;
        for (i = 0; i < branch->childCount; i++)
        {
            VMDispose(branch->children[i]);
        }
        free(base);
    }
}

VMBase *VMDisposeBranch(VMBranch *branch, uint keepBranch)
{
    VMBase *child = branch->children[keepBranch];
    uint i;
    for (i = 0; i < branch->childCount; i++)
    {
        if (i != keepBranch)
        {
            VMDispose(branch->children[i]);
        }
    }
    child->parent = branch->base.parent;
    free(branch);
    return child;
}

void VMReplaceChild(VM *vm, VM *child)
{
    if (DEBUG_VM)
    {
        printf("Replace child VM:%p child:%p\n", (void*)vm, (void*)child);
    }
    assert(child->base.parent == &vm->base);
    if (child->job)
    {
        vm->job = child->job;
        vm->job->vm = vm;
        child->job = null;
    }
    vm->child = child->child;
    if (vm->child)
    {
        vm->child->parent = &vm->base;
    }
    child->child = null;
    VMDispose(&child->base);
}

void VMHalt(VM *vm, vref failMessage)
{
    if (DEBUG_VM)
    {
        printf("Halt VM:%p\n", (void*)vm);
    }
    if (vm->child)
    {
        assert(vm->child->fullVM);
        VMDispose(vm->child);
        vm->child = null;
    }
    vm->idle = true;
    vm->failMessage = failMessage;
}

void VMFail(VM *vm, const char *msg, size_t msgSize)
{
    VMHalt(vm, VCreateString(msg, msgSize));
}

void VMFailf(VM *vm, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    VMHalt(vm, VCreateStringFormatted(format, args));
    va_end(args);
}


vref VMReadValue(VM *vm)
{
    int variable = *vm->ip++;
    if (variable >= 0)
    {
        return refFromInt(IVGet(&vm->stack, (size_t)(vm->bp + variable)));
    }
    if (-variable <= vm->constantCount)
    {
        return vm->constants[-variable - 1];
    }
    return vm->fields[-variable - vm->constantCount - 1];
}

void VMStoreValue(VM *vm, int variable, vref value)
{
    if (variable >= 0)
    {
        IVSet(&vm->stack, (size_t)(vm->bp + variable), intFromRef(value));
        return;
    }
    assert(-variable > vm->constantCount);
    vm->fields[-variable - vm->constantCount - 1] = value;
}
