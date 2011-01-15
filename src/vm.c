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
