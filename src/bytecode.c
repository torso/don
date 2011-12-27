#include <stdio.h>
#include "common.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "functionindex.h"
#include "instruction.h"
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

static void appendNumberAsString(bytevector *buffer, int number)
{
    uint i = (uint)number;
    size_t size;

    if (!number)
    {
        BVAdd(buffer, '0');
        return;
    }
    if (number < 0)
    {
        BVAdd(buffer, '-');
        i = (uint)-number;
    }
    size = BVSize(buffer);
    while (i)
    {
        BVInsert(buffer, size, (byte)('0' + i % 10));
        i /= 10;
    }
}

static const byte *disassemble(const byte *bytecode, const byte *base,
                               const byte **limit)
{
    uint ip = (uint)(bytecode - base);
    functionref function;
    nativefunctionref nativeFunction;
    uint parameterCount;
    uint argumentCount;
    uint value;
    ref_t ref;
    uint controlFlowNextInstruction = true;
    uint i;
    int argument;
    bytevector buffer;

    switch ((Instruction)*bytecode++)
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
        printf(" %u: push integer %d\n", ip, BytecodeReadInt(&bytecode));
        break;

    case OP_STRING:
        ref = BytecodeReadRef(&bytecode);
        printf(" %u: push string \"%s\"\n", ip, StringPoolGetString(ref));
        break;

    case OP_EMPTY_LIST:
        printf(" %u: push []\n", ip);
        break;

    case OP_LIST:
        printf(" %u: new list %u\n", ip, BytecodeReadUint(&bytecode));
        break;

    case OP_FILE:
        printf(" %u: file %s\n", ip,
               StringPoolGetString(BytecodeReadRef(&bytecode)));
        break;

    case OP_FILESET:
        printf(" %u: fileset %s\n", ip,
               StringPoolGetString(BytecodeReadRef(&bytecode)));
        break;

    case OP_POP:
        printf(" %u: pop\n", ip);
        break;

    case OP_DUP:
        printf(" %u: dup\n", ip);
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

    case OP_ITER_INIT:
        printf(" %u: iter_init\n", ip);
        break;

    case OP_ITER_NEXT:
        printf(" %u: iter_next\n", ip);
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
        argumentCount = BytecodeReadUint16(&bytecode);
        value = *bytecode++;
        printf(" %u: invoke \"%s\" arguments: %u return: %u\n",
               ip,
               StringPoolGetString(FunctionIndexGetName(function)),
               argumentCount, value);
        break;

    case OP_INVOKE_REORDER:
        function = BytecodeReadRef(&bytecode);
        argumentCount = BytecodeReadUint16(&bytecode);
        value = *bytecode++;
        parameterCount = FunctionIndexGetParameterCount(function);
        BVInit(&buffer, parameterCount * 5);
        for (i = 0; i < parameterCount; i++)
        {
            if (i)
            {
                BVAdd(&buffer, ',');
            }
            argument = BytecodeReadInt16(&bytecode);
            if (argument < 0)
            {
                BVAdd(&buffer, 's');
                BVAdd(&buffer, ':');
                appendNumberAsString(&buffer, argument);
            }
            else
            {
                switch (argument)
                {
                case FIELD_NULL:
                    BVAddData(&buffer, (const byte*)"null", 4);
                    break;

                case FIELD_TRUE:
                    BVAddData(&buffer, (const byte*)"true", 4);
                    break;

                case FIELD_FALSE:
                    BVAddData(&buffer, (const byte*)"false", 5);
                    break;

                case FIELD_EMPTY_LIST:
                    BVAddData(&buffer, (const byte*)"[]", 2);
                    break;

                default:
                    BVAdd(&buffer, 'f');
                    BVAdd(&buffer, ':');
                    appendNumberAsString(&buffer, argument);
                    break;
                }
            }
        }
        BVAdd(&buffer, 0);
        printf(" %u: invoke \"%s\"(%s) arguments: %u return: %u\n",
               ip,
               StringPoolGetString(FunctionIndexGetName(function)),
               BVGetPointer(&buffer, 0),
               argumentCount, value);
        BVDispose(&buffer);
        break;

    case OP_INVOKE_NATIVE:
        nativeFunction = refFromUint(*bytecode++);
        printf(" %u: invoke native \"%s\"\n",
               ip, StringPoolGetString(NativeGetName(nativeFunction)));
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
