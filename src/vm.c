#include "common.h"
#include "vm.h"

const byte *vmBytecode;

void VMInit(byte *bytecode)
{
    vmBytecode = bytecode;
}

void VMDispose(void)
{
    free((byte*)vmBytecode);
}
