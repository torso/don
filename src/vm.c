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

void VMPush(VM *vm, objectref value)
{
    IntVectorAddRef(&vm->stack, value);
}

void VMPushBoolean(VM *vm, boolean value)
{
    VMPush(vm, value ? HeapTrue : HeapFalse);
}
