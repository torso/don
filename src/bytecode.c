#include <stdio.h>
#include "common.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "functionindex.h"
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

static void printValue(const byte **bytecode)
{
    printf("r%u", BytecodeReadUint(bytecode));
}

static void printBinaryOperation(const byte **bytecode, const char *op, uint arg)
{
    int r1 = BytecodeReadInt(bytecode);
    int r2 = BytecodeReadInt(bytecode);
    printf("r%d %s r%d -> r%d\n", arg, op, r1, r2);
}

static const byte *disassemble(const byte *bytecode, const byte *base)
{
    uint ip = (uint)(bytecode - base);
    uint value;
    char *string;
    uint i = BytecodeReadUint(&bytecode);
    uint arg = i >> 8;

    switch ((Instruction)(i & 0xff))
    {
    case OP_FUNCTION:
    {
        functionref function = BytecodeReadRef(&bytecode);
        uint parameterCount = BytecodeReadUint(&bytecode);
        uint localsCount = BytecodeReadUint(&bytecode);

        printf("function %s parameters:%d locals:%d\n",
               function ? HeapGetString(FunctionIndexGetName(function)) : "unknown",
               parameterCount, localsCount);
        break;
    }

    case OP_NULL:
        printf("store_null -> r%u\n", arg);
        break;

    case OP_TRUE:
        printf("store_true -> r%u\n", arg);
        break;

    case OP_FALSE:
        printf("store_false -> r%u\n", arg);
        break;

    case OP_EMPTY_LIST:
        printf("store_{} -> r%u\n", arg);
        break;

    case OP_LIST:
        printf("new list %u {", arg);
        if (arg)
        {
            printValue(&bytecode);
            while (--arg)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
        }
        fputs("} -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_FILELIST:
        printf("filelist %s -> ", HeapGetString(refFromUint(arg)));
        printValue(&bytecode);
        puts("");
        break;

    case OP_PUSH:
        string = HeapDebug(refFromUint(BytecodeReadUint(&bytecode)), false);
        printf("push %s -> r%u\n", string, arg);
        free(string);
        break;

    case OP_COPY:
        printf("copy r%u -> r%u\n", arg, BytecodeReadUint(&bytecode));
        break;

    case OP_LOAD_FIELD:
        printf("load_field %u -> r%u\n", BytecodeReadUint(&bytecode), arg);
        break;

    case OP_STORE_FIELD:
        printf("store_field %u = r%u\n", BytecodeReadUint(&bytecode), arg);
        break;

    case OP_NOT:
        printf("not r%u -> r%u", arg, BytecodeReadUint(&bytecode));
        break;

    case OP_NEG:
        printf("neg r%u -> r%u", arg, BytecodeReadUint(&bytecode));
        break;

    case OP_INV:
        printf("inv r%u -> r%u", arg, BytecodeReadUint(&bytecode));
        break;

    case OP_ITER_GET:
        printf("iter_get r%u[", arg);
        printValue(&bytecode);
        fputs("] -> ", stdout);
        printValue(&bytecode);
        fputs(",", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_EQUALS:
        printBinaryOperation(&bytecode, "==", arg);
        break;

    case OP_NOT_EQUALS:
        printBinaryOperation(&bytecode, "!=", arg);
        break;

    case OP_LESS_EQUALS:
        printBinaryOperation(&bytecode, "<=", arg);
        break;

    case OP_GREATER_EQUALS:
        printBinaryOperation(&bytecode, ">=", arg);
        break;

    case OP_LESS:
        printBinaryOperation(&bytecode, "<", arg);
        break;

    case OP_GREATER:
        printBinaryOperation(&bytecode, ">", arg);
        break;

    case OP_AND:
        printBinaryOperation(&bytecode, "and", arg);
        break;

    case OP_ADD:
        printBinaryOperation(&bytecode, "+", arg);
        break;

    case OP_SUB:
        printBinaryOperation(&bytecode, "-", arg);
        break;

    case OP_MUL:
        printBinaryOperation(&bytecode, "*", arg);
        break;

    case OP_DIV:
        printBinaryOperation(&bytecode, "/", arg);
        break;

    case OP_REM:
        printBinaryOperation(&bytecode, "%", arg);
        break;

    case OP_CONCAT_STRING:
        printBinaryOperation(&bytecode, "\"\"", arg);
        break;

    case OP_CONCAT_LIST:
        fputs("concat_list ", stdout);
        printValue(&bytecode);
        fputs(",", stdout);
        printValue(&bytecode);
        fputs(" -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_INDEXED_ACCESS:
        fputs("indexed_access ", stdout);
        printValue(&bytecode);
        fputs("[", stdout);
        printValue(&bytecode);
        fputs("] -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_RANGE:
        printBinaryOperation(&bytecode, "..", arg);
        break;

    case OP_JUMP:
        value = BytecodeReadUint(&bytecode);
        printf("jump %u\n", (uint)(ip + 2 * sizeof(uint) + value));
        break;

    case OP_BRANCH_TRUE:
        value = BytecodeReadUint(&bytecode);
        printf("branch_true r%u, %u\n", arg, (uint)(ip + 2 * sizeof(uint) + value));
        break;

    case OP_BRANCH_FALSE:
        value = BytecodeReadUint(&bytecode);
        printf("branch_false r%u, %u\n", arg, (uint)(ip + 2 * sizeof(uint) + value));
        break;

    case OP_RETURN:
        assert(arg > 0);
        fputs("return {", stdout);
        printValue(&bytecode);
        while (--arg)
        {
            fputs(",", stdout);
            printValue(&bytecode);
        }
        puts("}");
        break;

    case OP_RETURN_VOID:
        puts("return");
        break;

    case OP_INVOKE:
    {
        functionref function = refFromUint(arg);
        uint argumentCount = BytecodeReadUint(&bytecode);
        uint returnCount;
        printf("invoke %s(", HeapGetString(FunctionIndexGetName(function)));
        if (argumentCount)
        {
            printValue(&bytecode);
            while (--argumentCount)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
        }
        returnCount = BytecodeReadUint(&bytecode);
        if (returnCount)
        {
            fputs(") -> ", stdout);
            printValue(&bytecode);
            while (--returnCount)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
            puts("");
        }
        else
        {
            puts(")");
        }
        break;
    }

    case OP_INVOKE_NATIVE:
    {
        nativefunctionref nativeFunction = refFromUint(arg);
        uint count = NativeGetParameterCount(nativeFunction);
        printf("invoke native %s(", HeapGetString(NativeGetName(nativeFunction)));
        if (count)
        {
            printValue(&bytecode);
            while (--count)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
        }
        count = NativeGetReturnValueCount(nativeFunction);
        if (count)
        {
            fputs(") -> ", stdout);
            printValue(&bytecode);
            while (--count)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
            puts("");
        }
        else
        {
            puts(")");
        }
        break;
    }

    case OP_UNKNOWN_VALUE:
        puts("unknown_value");
        break;
    }
    return bytecode;
}

const byte *BytecodeDisassembleInstruction(const byte *bytecode,
                                           const byte *base)
{
    return disassemble(bytecode, base);
}

void BytecodeDisassemble(const byte *bytecode, const byte *bytecodeLimit)
{
    const byte *start = bytecode;

    while (bytecode < bytecodeLimit)
    {
        printf(" %u: ", (uint)(bytecode - start));
        bytecode = disassemble(bytecode, start);
    }
}
