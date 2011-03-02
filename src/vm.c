#include "common.h"
#include "vm.h"

const byte *vmBytecode;


void VMInit(byte *bytecode)
{
    vmBytecode = bytecode;
    HeapInit();
}

void VMDispose(void)
{
    HeapDispose();
    free((byte*)vmBytecode);
}


objectref VMPeek(VM *vm)
{
    return IntVectorPeekRef(&vm->stack);
}

objectref VMPop(VM *vm)
{
    return IntVectorPopRef(&vm->stack);
}

boolean VMPopBoolean(VM *vm)
{
    return HeapIsTrue(IntVectorPopRef(&vm->stack));
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
