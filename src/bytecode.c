#include <stdlib.h>
#include "builder.h"
#include "bytecode.h"
#include "bytevector.h"
#include "instruction.h"
#include "native.h"
#include "stringpool.h"
#include "targetindex.h"

uint BytecodeDisassembleInstruction(const bytevector *bytecode, uint offset)
{
    uint ip = offset;
    uint function;
    uint arguments;
    uint value;

    switch ((Instruction)(int)ByteVectorRead(bytecode, &offset))
    {
    case OP_NULL:
        printf(" %u: push null\n", ip);
        break;

    case OP_TRUE:
        printf(" %u: push true\n", ip);
        break;

    case OP_FALSE:
        printf(" %u: push false\n", ip);
        break;

    case OP_INTEGER:
        printf(" %u: push integer %d\n", ip, ByteVectorReadPackInt(bytecode, &offset));
        break;

    case OP_STRING:
        value = ByteVectorReadPackUint(bytecode, &offset);
        printf(" %u: push string %u \"%s\"\n", ip, value,
               StringPoolGetString((stringref)value));
        break;

    case OP_LOAD:
        printf(" %u: load %u\n", ip, ByteVectorReadPackUint(bytecode, &offset));
        break;

    case OP_STORE:
        printf(" %u: store %u\n", ip, ByteVectorReadPackUint(bytecode, &offset));
        break;

    case OP_EQUALS:
        printf(" %u: equals\n", ip);
        break;

    case OP_NOT_EQUALS:
        printf(" %u: !equals\n", ip);
        break;

    case OP_ADD:
        printf(" %u: add\n", ip);
        break;

    case OP_SUB:
        printf(" %u: sub\n", ip);
        break;

    case OP_JUMP:
        value = (uint)ByteVectorReadPackInt(bytecode, &offset);
        printf(" %u: jump %u\n", ip, offset + value);
        break;

    case OP_BRANCH_FALSE:
        value = (uint)ByteVectorReadPackInt(bytecode, &offset);
        printf(" %u: branch_false %u\n", ip, offset + value);
        break;

    case OP_RETURN:
        printf(" %u: return %u\n", ip,
               ByteVectorReadPackUint(bytecode, &offset));
        break;

    case OP_RETURN_VOID:
        printf(" %u: return\n", ip);
        break;

    case OP_INVOKE:
        function = ByteVectorReadPackUint(bytecode, &offset);
        arguments = ByteVectorReadPackUint(bytecode, &offset);
        value = ByteVectorReadPackUint(bytecode, &offset);
        printf(" %u: invoke %u \"%s\" arguments: %u return: %u\n",
               ip, function,
               StringPoolGetString(TargetIndexGetName((targetref)function)),
               arguments, value);
        break;

    case OP_INVOKE_NATIVE:
        function = ByteVectorRead(bytecode, &offset);
        arguments = ByteVectorReadPackUint(bytecode, &offset);
        value = ByteVectorReadPackUint(bytecode, &offset);
        printf(" %u: invoke native %u \"%s\" arguments: %u return: %u\n",
               ip, function,
               StringPoolGetString(NativeGetName((nativefunctionref)function)),
               arguments, value);
        break;
    }
    return offset;
}

void BytecodeDisassembleFunction(const bytevector *bytecode, uint offset)
{
    while (offset < ByteVectorSize(bytecode))
    {
        offset = BytecodeDisassembleInstruction(bytecode, offset);
    }
}
