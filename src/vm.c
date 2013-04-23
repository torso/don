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
    byte *data = (byte*)malloc(sizeof(VM) + FieldIndexGetCount() * sizeof(objectref));
    VM *vm = (VM*)data;
    vm->fields = (objectref*)(data + sizeof(VM));
    IVInit(&vm->callStack, 128);
    IVInit(&vm->stack, 1024);
    return vm;
}

VM *VMCreate(const byte *bytecode, functionref target)
{
    VM *vm = VMAlloc();

    FieldIndexCopyValues(vm->fields);
    vm->base.parent = null;
    vm->base.condition = HeapTrue;

    vm->target = target;
    vm->ip = bytecode +
        FunctionIndexGetBytecodeOffset(FunctionIndexGetFirstFunction());
    vm->bp = 0;
    return vm;
}

VM *VMClone(VM *vm, objectref condition, const byte *ip)
{
    VM *clone = VMAlloc();
    VMBranch *parent = (VMBranch*)malloc(sizeof(VMBranch));

    parent->base.parent = vm->base.parent;
    parent->base.condition = vm->base.condition;
    parent->base.childCount = 2;

    vm->base.parent = parent;
    clone->base.parent = parent;
    memcpy(clone->fields, vm->fields, FieldIndexGetCount() * sizeof(objectref));
    IVAppendAll(&vm->callStack, &clone->callStack);
    IVAppendAll(&vm->stack, &clone->stack);
    clone->target = vm->target;
    clone->ip = ip;
    clone->bp = vm->bp;

    clone->base.condition = HeapApplyBinary(clone, OP_AND,
                                            vm->base.condition, condition);
    vm->base.condition = HeapApplyBinary(vm, OP_AND, vm->base.condition,
                                         HeapApplyUnary(vm, OP_NOT, condition));

    return clone;
}

void VMDispose(VM *vm)
{
    VMBranch *parent = vm->base.parent;
    VMBranch *next;

    WorkDiscard(vm);
    while (parent)
    {
        if (--parent->base.childCount)
        {
            break;
        }
        next = parent->base.parent;
        free(parent);
        parent = next;
    }
    IVDispose(&vm->callStack);
    IVDispose(&vm->stack);
    free(vm);
}


objectref VMPeek(VM *vm)
{
    return IVPeekRef(&vm->stack);
}

objectref VMPop(VM *vm)
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

void VMPopMany(VM *vm, objectref *dst, uint count)
{
    dst += count - 1;
    while (count--)
    {
        *dst-- = VMPop(vm);
    }
}

void VMPush(VM *vm, objectref value)
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

void VMPushMany(VM *vm, const objectref *values, uint count)
{
    while (count--)
    {
        VMPush(vm, *values++);
    }
}
