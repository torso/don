#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
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
    vm->ip = bytecode +
        FunctionIndexGetBytecodeOffset(FunctionIndexGetFirstFunction());
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


vref VMPeek(VM *vm)
{
    return IVPeekRef(&vm->stack);
}

vref VMPop(VM *vm)
{
    char *buffer;
    if (TRACE_STACK)
    {
        buffer = HeapDebug(IVPeek(&vm->stack), true);
        printf("pop %s\n", buffer);
        free(buffer);
    }
    return IVPopRef(&vm->stack);
}

void VMPopMany(VM *vm, vref *dst, uint count)
{
    dst += count - 1;
    while (count--)
    {
        *dst-- = VMPop(vm);
    }
}

void VMPush(VM *vm, vref value)
{
    char *buffer;
    if (TRACE_STACK)
    {
        buffer = HeapDebug(value, true);
        printf("push %s\n", buffer);
        free(buffer);
    }
    IVAddRef(&vm->stack, value);
}

void VMPushBoolean(VM *vm, boolean value)
{
    VMPush(vm, value ? HeapTrue : HeapFalse);
}

void VMPushMany(VM *vm, const vref *values, uint count)
{
    while (count--)
    {
        VMPush(vm, *values++);
    }
}
