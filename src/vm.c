#include <memory.h>
#include "common.h"
#include "vm.h"
#include "fieldindex.h"
#include "functionindex.h"
#include "work.h"

static VM *VMAlloc(void)
{
    byte *data = (byte*)malloc(sizeof(VM) + FieldIndexGetCount() * sizeof(objectref));
    VM *vm = (VM*)data;
    vm->fields = (objectref*)(data + sizeof(VM));
    IVInit(&vm->mutableIndex, 4);
    IVInit(&vm->callStack, 128);
    IVInit(&vm->stack, 1024);
    return vm;
}

VM *VMCreate(const byte *bytecode, functionref target)
{
    VM *vm = VMAlloc();

    memset(vm->fields, 0, FieldIndexGetCount() * sizeof(objectref));
    vm->parent = null;
    vm->condition = HeapTrue;
    vm->mutableCount = 0;

    vm->fields[FIELD_TRUE] = HeapTrue;
    vm->fields[FIELD_FALSE] = HeapFalse;
    vm->fields[FIELD_EMPTY_LIST] = HeapEmptyList;

    vm->target = target;
    vm->ip = bytecode +
        FunctionIndexGetBytecodeOffset(FunctionIndexGetFirstFunction());
    vm->bp = 0;
    return vm;
}

VM *VMClone(VM *vm, objectref condition, const byte *ip)
{
    VM *clone = VMAlloc();
    uint mutableCount = (uint)IVSize(&vm->mutableIndex);
    VMBranch *parent = (VMBranch*)malloc(
        sizeof(VMBranch) - sizeof(parent->mutableIndex) +
        mutableCount * sizeof(parent->mutableIndex[0]));

    assert(IVSize(&vm->mutableIndex) < UINT_MAX);

    parent->parent = vm->parent;
    parent->condition = vm->condition;
    parent->children = 2;
    parent->mutableCount = mutableCount;
    if (mutableCount)
    {
        memcpy(parent->mutableIndex, IVGetPointer(&vm->mutableIndex, 0),
               mutableCount * sizeof(parent->mutableIndex[0]));
    }
    IVSetSize(&vm->mutableIndex, 0);

    vm->parent = parent;
    clone->parent = parent;
    clone->mutableCount = vm->mutableCount;
    memcpy(clone->fields, vm->fields, FieldIndexGetCount() * sizeof(objectref));
    IVAppendAll(&vm->callStack, &clone->callStack);
    IVAppendAll(&vm->stack, &clone->stack);
    clone->target = vm->target;
    clone->ip = ip;
    clone->bp = vm->bp;

    clone->condition = HeapApplyBinary(clone, OP_AND, vm->condition, condition);
    vm->condition = HeapApplyBinary(vm, OP_AND, vm->condition,
                                    HeapApplyUnary(vm, OP_NOT, condition));

    return clone;
}

void VMDispose(VM *vm)
{
    VMBranch *parent = vm->parent;
    VMBranch *next;

    WorkDiscard(vm);
    while (parent)
    {
        if (--parent->children)
        {
            break;
        }
        next = parent->parent;
        free(parent);
        parent = next;
    }
    IVDispose(&vm->mutableIndex);
    IVDispose(&vm->callStack);
    IVDispose(&vm->stack);
    free(vm);
}


uint VMAddMutable(VM *vm, objectref object)
{
    assert(IVSize(&vm->mutableIndex) <= vm->mutableCount);
    IVSetSize(&vm->mutableIndex, vm->mutableCount + 1);
    IVSetRef(&vm->mutableIndex, vm->mutableCount, object);
    return vm->mutableCount++;
}

objectref VMGetMutable(VM *vm, uint index)
{
    VMBranch *branch;
    objectref object;

    assert(index < vm->mutableCount);
    if (IVSize(&vm->mutableIndex) > index)
    {
        object = IVGetRef(&vm->mutableIndex, index);
        if (object)
        {
            return object;
        }
    }

    for (branch = vm->parent;; branch = branch->parent)
    {
        assert(branch);
        if (branch->mutableCount > index)
        {
            object = branch->mutableIndex[index];
            if (object)
            {
                if (branch->children != 1)
                {
                    object = HeapClone(object);
                    if (IVSize(&vm->mutableIndex) <= index)
                    {
                        IVSetSize(&vm->mutableIndex, index + 1);
                    }
                    IVSetRef(&vm->mutableIndex, index, object);
                }
                return object;
            }
        }
    }
}


objectref VMPeek(VM *vm)
{
    return IVPeekRef(&vm->stack);
}

objectref VMPop(VM *vm)
{
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
