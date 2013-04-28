#include <stdio.h>
#include "common.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "functionindex.h"
#include "instruction.h"
#include "heap.h"
#include "native.h"
#include "stringpool.h"

#define MAX(a, b) (a > b ? a : b)

uint BytecodeReadUint(const byte **bytecode)
{
    uint value = *(uint*)*bytecode;
    *bytecode += sizeof(uint);
    return value;
}

int16 BytecodeReadInt16(const byte **bytecode)
{
    byte value = *(*bytecode)++;
    return (int16)((value << 8) + *(*bytecode)++);
}

uint16 BytecodeReadUint16(const byte **bytecode)
{
    byte value = *(*bytecode)++;
    return (uint16)((value << 8) + *(*bytecode)++);
}

ref_t BytecodeReadRef(const byte **bytecode)
{
    return refFromUint(BytecodeReadUint(bytecode));
}

static const byte *disassemble(const byte *bytecode, const byte *base,
                               const byte **limit)
{
    uint ip = (uint)(bytecode - base);
    functionref function;
    nativefunctionref nativeFunction;
    uint value;
    uint i;
    uint controlFlowNextInstruction = true;
    char *string;

    switch ((Instruction)*bytecode++)
    {
    case OP_PUSH:
        string = HeapDebug(refFromUint(BytecodeReadUint(&bytecode)), false);
        printf(" %u: push %s\n", ip, string);
        free(string);
        break;

    case OP_NULL:
        printf(" %u: push_null\n", ip);
        break;

    case OP_TRUE:
        printf(" %u: push_true\n", ip);
        break;

    case OP_FALSE:
        printf(" %u: push_false\n", ip);
        break;

    case OP_EMPTY_LIST:
        printf(" %u: push_{}\n", ip);
        break;

    case OP_LIST:
        printf(" %u: new list %u\n", ip, BytecodeReadUint(&bytecode));
        break;

    case OP_FILESET:
        printf(" %u: fileset %s\n", ip,
               HeapGetString(BytecodeReadRef(&bytecode)));
        break;

    case OP_POP:
        printf(" %u: pop\n", ip);
        break;

    case OP_DUP:
        printf(" %u: dup\n", ip);
        break;

    case OP_REORDER_STACK:
        value = BytecodeReadUint16(&bytecode);
        printf(" %u: reorder_stack %u\n", ip, value);
        for (i = 0; i < value; i++)
        {
            printf("    %u -> %u\n", i, BytecodeReadUint16(&bytecode));
        }
        break;

    case OP_LOAD:
        printf(" %u: load %u\n", ip, BytecodeReadUint16(&bytecode));
        break;

    case OP_STORE:
        printf(" %u: store %u\n", ip, BytecodeReadUint16(&bytecode));
        break;

    case OP_LOAD_FIELD:
        printf(" %u: load_field %u\n", ip, BytecodeReadUint(&bytecode));
        break;

    case OP_STORE_FIELD:
        printf(" %u: store_field %u\n", ip, BytecodeReadUint(&bytecode));
        break;

    case OP_CAST_BOOLEAN:
        printf(" %u: cast_boolean\n", ip);
        break;

    case OP_NOT:
        printf(" %u: not\n", ip);
        break;

    case OP_NEG:
        printf(" %u: neg\n", ip);
        break;

    case OP_INV:
        printf(" %u: inv\n", ip);
        break;

    case OP_ITER_GET:
        printf(" %u: iter_get\n", ip);
        break;

    case OP_EQUALS:
        printf(" %u: equals\n", ip);
        break;

    case OP_NOT_EQUALS:
        printf(" %u: !equals\n", ip);
        break;

    case OP_LESS_EQUALS:
        printf(" %u: lessequals\n", ip);
        break;

    case OP_GREATER_EQUALS:
        printf(" %u: greaterequals\n", ip);
        break;

    case OP_LESS:
        printf(" %u: less\n", ip);
        break;

    case OP_GREATER:
        printf(" %u: greater\n", ip);
        break;

    case OP_AND:
        printf(" %u: and\n", ip);
        break;

    case OP_ADD:
        printf(" %u: add\n", ip);
        break;

    case OP_SUB:
        printf(" %u: sub\n", ip);
        break;

    case OP_MUL:
        printf(" %u: mul\n", ip);
        break;

    case OP_DIV:
        printf(" %u: div\n", ip);
        break;

    case OP_REM:
        printf(" %u: rem\n", ip);
        break;

    case OP_CONCAT_STRING:
        printf(" %u: concat_string\n", ip);
        break;

    case OP_CONCAT_LIST:
        printf(" %u: concat_list\n", ip);
        break;

    case OP_INDEXED_ACCESS:
        printf(" %u: indexed_access\n", ip);
        break;

    case OP_RANGE:
        printf(" %u: range\n", ip);
        break;

    case OP_JUMP:
        value = BytecodeReadUint(&bytecode);
        printf(" %u: jump %u\n", ip, (uint)(ip + 1 + sizeof(uint) + value));
        *limit = MAX(*limit, bytecode + (int)value);
        controlFlowNextInstruction = false;
        break;

    case OP_BRANCH_TRUE:
        value = BytecodeReadUint(&bytecode);
        printf(" %u: branch_true %u\n", ip, (uint)(ip + 1 + sizeof(uint) + value));
        *limit = MAX(*limit, bytecode + (int)value);
        controlFlowNextInstruction = false;
        break;

    case OP_BRANCH_FALSE:
        value = BytecodeReadUint(&bytecode);
        printf(" %u: branch_false %u\n", ip, (uint)(ip + 1 + sizeof(uint) + value));
        *limit = MAX(*limit, bytecode + (int)value);
        controlFlowNextInstruction = false;
        break;

    case OP_RETURN:
        printf(" %u: return %u\n", ip, *bytecode++);
        controlFlowNextInstruction = false;
        break;

    case OP_RETURN_VOID:
        printf(" %u: return\n", ip);
        controlFlowNextInstruction = false;
        break;

    case OP_INVOKE:
        function = BytecodeReadRef(&bytecode);
        value = *bytecode++;
        printf(" %u: invoke \"%s\" return: %u\n", ip,
               HeapGetString(FunctionIndexGetName(function)), value);
        break;

    case OP_INVOKE_NATIVE:
        nativeFunction = refFromUint(*bytecode++);
        printf(" %u: invoke native \"%s\"\n",
               ip, HeapGetString(NativeGetName(nativeFunction)));
        break;

    case OP_UNKNOWN_VALUE:
        printf("  %u: unknown_value\n", ip);
        break;
    }
    if (controlFlowNextInstruction)
    {
        *limit = MAX(*limit, bytecode);
    }
    return bytecode;
}

const byte *BytecodeDisassembleInstruction(const byte *bytecode,
                                           const byte *base)
{
    const byte *limit;
    return disassemble(bytecode, base, &limit);
}

void BytecodeDisassembleFunction(const byte *bytecode,
                                 const byte *bytecodeLimit)
{
    const byte *start = bytecode;
    const byte *limit = bytecode;

    do
    {
        bytecode = disassemble(bytecode, start, &limit);
    }
    while (bytecode <= limit && bytecode < bytecodeLimit);
}
