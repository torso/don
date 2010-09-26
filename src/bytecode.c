#include "builder.h"
#include "bytecode.h"
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

uint16 BytecodeReadUint16(const byte **bytecode)
{
    byte value = *(*bytecode)++;
    return (uint16)((value << 16) + *(*bytecode)++);
}

static const byte *disassemble(const byte *bytecode, const byte *base,
                               const byte **limit)
{
    uint ip = (uint)(bytecode - base);
    uint function;
    uint arguments;
    uint value;
    uint controlFlowNextInstruction = true;

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
        value = BytecodeReadUint(&bytecode);
        printf(" %u: push string %u \"%s\"\n", ip, value,
               StringPoolGetString((stringref)value));
        break;

    case OP_EMPTY_LIST:
        printf(" %u: push []\n", ip);
        break;

    case OP_LIST:
        printf(" %u: new list %u\n", ip, BytecodeReadUint(&bytecode));
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

    case OP_CAST_BOOLEAN:
        printf(" %u: cast_boolean\n", ip);
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

    case OP_NOT:
        printf(" %u: not\n", ip);
        break;

    case OP_NEG:
        printf(" %u: neg\n", ip);
        break;

    case OP_INV:
        printf(" %u: inv\n", ip);
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

    case OP_CONCAT:
        printf(" %u: concat\n", ip);
        break;

    case OP_RANGE:
        printf(" %u: range\n", ip);
        break;

    case OP_ITER_INIT:
        printf(" %u: iter_init\n", ip);
        break;

    case OP_ITER_NEXT:
        printf(" %u: iter_next\n", ip);
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
        function = BytecodeReadUint(&bytecode);
        arguments = BytecodeReadUint16(&bytecode);
        value = *bytecode++;
        printf(" %u: invoke %u \"%s\" arguments: %u return: %u\n",
               ip, function,
               StringPoolGetString(FunctionIndexGetName((functionref)function)),
               arguments, value);
        break;

    case OP_INVOKE_NATIVE:
        function = *bytecode++;
        arguments = BytecodeReadUint16(&bytecode);
        value = *bytecode++;
        printf(" %u: invoke native %u \"%s\" arguments: %u return: %u\n",
               ip, function,
               StringPoolGetString(NativeGetName((nativefunctionref)function)),
               arguments, value);
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

void BytecodeDisassembleFunction(const byte *bytecode)
{
    const byte *start = bytecode;
    const byte *limit = bytecode + 1;

    while (bytecode <= limit)
    {
        bytecode = disassemble(bytecode, start, &limit);
    }
}
