#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "linker.h"
#include "heap.h"
#include "instruction.h"
#include "work.h"
#include "vm.h"

const int *vmBytecode;
const int *vmLineNumbers;

static VM *VMAlloc(VMBranch *parent, int fieldCount)
{
    byte *data = (byte*)calloc(
        sizeof(VM) + (uint)fieldCount * sizeof(vref), 1);
    VM *vm = (VM*)data;
    vm->branch = (VMBranch*)malloc(sizeof(VMBranch));
    vm->branch->parent = parent;
    vm->branch->condition = VTrue;
    vm->branch->childCount = 1;
    vm->branch->children = (void**)malloc(2 * sizeof(void*));
    vm->branch->children[0] = vm;
    vm->branch->leaf = true;
    vm->fields = (vref*)(data + sizeof(VM));
    vm->fieldCount = fieldCount;
    IVInit(&vm->callStack, 128);
    IVInit(&vm->stack, 1024);
    vm->active = true;
    vm->failMessage = 0;
    return vm;
}

VM *VMCreate(const LinkedProgram *program)
{
    VM *vm = VMAlloc(null, program->fieldCount);
    vmBytecode = program->bytecode;
    vmLineNumbers = program->lineNumbers;
    vm->constants = program->constants;
    vm->constantCount = program->constantCount;
    vm->bp = 0;
    memcpy(vm->fields, program->fields, (uint)vm->fieldCount * sizeof(*vm->fields));
    return vm;
}

/*
  Before:
    vm->branch->parent (VMBranch 1)
     |
    vm->branch (VMBranch 2)
     |
    vm

  After:
    (vm/clone)->branch->parent (VMBranch 1)
                 |
    (vm/clone)->branch->parent (VMBranch 2)
     |                 |
    vm->branch (new)  clone->branch (new)
     |                 |
    vm                clone (new)
*/
VM *VMClone(VM *vm, vref condition, const int *ip)
{
    VM *clone = VMAlloc(vm->branch, vm->fieldCount);
    VMBranch *newBranch = (VMBranch*)malloc(sizeof(VMBranch));
    vref parentCondition = vm->branch->condition;
    vref notCondition;

    newBranch->parent = vm->branch;
    newBranch->childCount = 1;
    newBranch->children = (void**)malloc(2 * sizeof(void*));
    newBranch->children[0] = vm;
    newBranch->leaf = true;
    assert(newBranch->parent->childCount == 1); /* TODO */
    newBranch->parent->childCount = 2;
    newBranch->parent->children[0] = newBranch;
    newBranch->parent->children[1] = clone->branch;
    newBranch->parent->leaf = false;
    vm->branch = newBranch;

    assert(condition);
    assert(newBranch->parent->condition);
    clone->branch->condition = VTrue;
    clone->branch->condition = VAnd(clone, parentCondition, condition);
    vm->branch->condition = VTrue;
    notCondition = VNot(vm, condition);
    vm->branch->condition = VAnd(vm, parentCondition, notCondition);

    clone->constants = vm->constants;
    clone->constantCount = vm->constantCount;
    memcpy(clone->fields, vm->fields, (uint)vm->fieldCount * sizeof(*vm->fields));
    clone->fieldCount = vm->fieldCount;
    IVAppendAll(&vm->callStack, &clone->callStack);
    IVAppendAll(&vm->stack, &clone->stack);
    clone->ip = ip;
    clone->bp = vm->bp;

    return clone;
}

void VMDispose(VM *vm)
{
    VMBranch *parent = vm->branch;
    VMBranch *child = null;

    while (parent)
    {
        if (--parent->childCount)
        {
            int i;
            for (i = (int)parent->childCount; i >= 0; i--)
            {
                if (parent->children[i] == child)
                {
                    memmove(parent->children + i, parent->children + i + 1,
                            (parent->childCount - (uint)i) * sizeof(void*));
                    break;
                }
            }
            break;
        }
        WorkDiscard(parent);
        child = parent;
        parent = parent->parent;
        free(child->children);
        free(child);
    }
    IVDispose(&vm->callStack);
    IVDispose(&vm->stack);
    free(vm);
}

void VMHalt(VM *vm, vref failMessage)
{
    vm->active = false;
    vm->failMessage = failMessage;
}

void VMFail(VM *vm, const int *ip, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vm->ip = ip;
    VMHalt(vm, VCreateStringFormatted(format, args));
    va_end(args);
}

void VMBranchFail(VMBranch *branch, const int *ip, vref failMessage)
{
    uint i;
    for (i = 0; i < branch->childCount; i++)
    {
        if (branch->leaf)
        {
            VM *vm = (VM*)branch->children[i];
            vm->ip = ip;
            VMHalt(vm, failMessage);
        }
        else
        {
            VMBranch *child = (VMBranch*)branch->children[i];
            child->condition = VFalse;
            VMBranchFail(child, ip, failMessage);
        }
    }
}

void VMBranchFailf(VMBranch *branch, const int *ip, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    VMBranchFail(branch, ip, VCreateStringFormatted(format, args));
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
