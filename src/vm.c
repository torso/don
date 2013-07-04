#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "fieldindex.h"
#include "functionindex.h"
#include "work.h"

static const boolean TRACE_STACK = false;

static VM *VMAlloc(void)
{
    byte *data = (byte*)calloc(
        sizeof(VM) + FieldIndexGetCount() * sizeof(vref), 1);
    VM *vm = (VM*)data;
    vm->fields = (vref*)(data + sizeof(VM));
    IVInit(&vm->callStack, 128);
    IVInit(&vm->stack, 1024);
    return vm;
}

VM *VMCreate(const byte *bytecode, functionref target)
{
    VM *vm = VMAlloc();
    vm->parent = null;
    vm->condition = HeapTrue;
    vm->target = target;
    vm->ip = bytecode;
    vm->bp = 0;
    return vm;
}

VM *VMClone(VM *vm, vref condition, const byte *ip)
{
    VM *clone = VMAlloc();
    VMBranch *parent = (VMBranch*)malloc(sizeof(VMBranch));

    parent->parent = vm->parent;
    parent->condition = vm->condition;
    parent->childCount = 2;

    vm->parent = parent;
    clone->parent = parent;
    memcpy(clone->fields, vm->fields, FieldIndexGetCount() * sizeof(vref));
    IVAppendAll(&vm->callStack, &clone->callStack);
    IVAppendAll(&vm->stack, &clone->stack);
    clone->target = vm->target;
    clone->ip = ip;
    clone->bp = vm->bp;

    clone->condition = HeapApplyBinary(OP_AND, vm->condition, condition);
    vm->condition = HeapApplyBinary(OP_AND, vm->condition,
                                    HeapApplyUnary(OP_NOT, condition));

    return clone;
}

void VMDispose(VM *vm)
{
    VMBranch *parent = vm->parent;
    VMBranch *next;

    WorkDiscard(vm);
    while (parent)
    {
        if (--parent->childCount)
        {
            break;
        }
        next = parent->parent;
        free(parent);
        parent = next;
    }
    IVDispose(&vm->callStack);
    IVDispose(&vm->stack);
    free(vm);
}


vref VMReadValue(VM *vm)
{
    return IVGetRef(&vm->stack, vm->bp + BytecodeReadUint(&vm->ip));
}
