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
    IntVectorInit(&vm->callStack);
    IntVectorInit(&vm->stack);
    return vm;
}

VM *VMCreate(const byte *bytecode, functionref target)
{
    VM *vm = VMAlloc();

    memset(vm->fields, 0, FieldIndexGetCount() * sizeof(objectref));
    vm->condition = HeapTrue;

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
    clone->condition = HeapApplyBinary(OP_AND, vm->condition, condition);
    memcpy(clone->fields, vm->fields, FieldIndexGetCount() * sizeof(objectref));
    IntVectorAppendAll(&vm->callStack, &clone->callStack);
    IntVectorAppendAll(&vm->stack, &clone->stack);
    clone->target = vm->target;
    clone->ip = ip;
    clone->bp = vm->bp;
    return clone;
}

void VMDispose(VM *vm)
{
    WorkDiscard(vm);
    IntVectorDispose(&vm->callStack);
    IntVectorDispose(&vm->stack);
    free(vm);
}


void VMApplyCondition(VM *vm, objectref condition)
{
    vm->condition = HeapApplyBinary(OP_AND, vm->condition, condition);
}


objectref VMPeek(VM *vm)
{
    return IntVectorPeekRef(&vm->stack);
}

objectref VMPop(VM *vm)
{
    return IntVectorPopRef(&vm->stack);
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
    IntVectorAddRef(&vm->stack, value);
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
