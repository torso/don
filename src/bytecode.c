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

static void printBinaryOperation(const byte **bytecode, const char *op)
{
    int r1 = BytecodeReadInt(bytecode);
    int r2 = BytecodeReadInt(bytecode);
    int r3 = BytecodeReadInt(bytecode);
    printf("r%d %s r%d -> r%d\n", r1, op, r2, r3);
}

static const byte *disassemble(const byte *bytecode, const byte *base)
{
    uint ip = (uint)(bytecode - base);
    uint value;
    char *string;

    switch ((Instruction)*bytecode++)
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
        fputs("store_null -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_TRUE:
        fputs("store_true -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_FALSE:
        fputs("store_false -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_EMPTY_LIST:
        fputs("store_{} -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_LIST:
    {
        uint count = BytecodeReadUint(&bytecode);
        printf("new list %u {", count);
        if (count)
        {
            printValue(&bytecode);
            while (--count)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
        }
        fputs("} -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;
    }

    case OP_FILELIST:
        printf("filelist %s -> ", HeapGetString(BytecodeReadRef(&bytecode)));
        printValue(&bytecode);
        puts("");
        break;

    case OP_PUSH:
        string = HeapDebug(refFromUint(BytecodeReadUint(&bytecode)), false);
        printf("push %s -> ", string);
        printValue(&bytecode);
        puts("");
        free(string);
        break;

    case OP_COPY:
    {
        uint src = BytecodeReadUint(&bytecode);
        uint dst = BytecodeReadUint(&bytecode);
        printf("copy r%u -> r%u\n", src, dst);
        break;
    }

    case OP_LOAD_FIELD:
        printf("load_field %u -> ", BytecodeReadUint(&bytecode));
        printValue(&bytecode);
        puts("");
        break;

    case OP_STORE_FIELD:
        printf("store_field %u = ", BytecodeReadUint(&bytecode));
        printValue(&bytecode);
        puts("");
        break;

    case OP_NOT:
        fputs("not ", stdout);
        printValue(&bytecode);
        fputs(" -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_NEG:
        fputs("neg ", stdout);
        printValue(&bytecode);
        fputs(" -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_INV:
        fputs("inv ", stdout);
        printValue(&bytecode);
        fputs(" -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_ITER_GET:
        fputs("iter_get ", stdout);
        printValue(&bytecode);
        fputs("[", stdout);
        printValue(&bytecode);
        fputs("] -> ", stdout);
        printValue(&bytecode);
        fputs(",", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_EQUALS:
        printBinaryOperation(&bytecode, "==");
        break;

    case OP_NOT_EQUALS:
        printBinaryOperation(&bytecode, "!=");
        break;

    case OP_LESS_EQUALS:
        printBinaryOperation(&bytecode, "<=");
        break;

    case OP_GREATER_EQUALS:
        printBinaryOperation(&bytecode, ">=");
        break;

    case OP_LESS:
        printBinaryOperation(&bytecode, "<");
        break;

    case OP_GREATER:
        printBinaryOperation(&bytecode, ">");
        break;

    case OP_AND:
        printBinaryOperation(&bytecode, "and");
        break;

    case OP_ADD:
        printBinaryOperation(&bytecode, "+");
        break;

    case OP_SUB:
        printBinaryOperation(&bytecode, "-");
        break;

    case OP_MUL:
        printBinaryOperation(&bytecode, "*");
        break;

    case OP_DIV:
        printBinaryOperation(&bytecode, "/");
        break;

    case OP_REM:
        printBinaryOperation(&bytecode, "%");
        break;

    case OP_CONCAT_STRING:
        printBinaryOperation(&bytecode, "\"\"");
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
        printBinaryOperation(&bytecode, "..");
        break;

    case OP_JUMP:
        value = BytecodeReadUint(&bytecode);
        printf("jump %u\n", (uint)(ip + 1 + sizeof(uint) + value));
        break;

    case OP_BRANCH_TRUE:
        fputs("branch_true ", stdout);
        printValue(&bytecode);
        value = BytecodeReadUint(&bytecode);
        printf(", %u\n", (uint)(ip + 1 + 2 * sizeof(uint) + value));
        break;

    case OP_BRANCH_FALSE:
        fputs("branch_false ", stdout);
        printValue(&bytecode);
        value = BytecodeReadUint(&bytecode);
        printf(", %u\n", (uint)(ip + 1 + 2 * sizeof(uint) + value));
        break;

    case OP_RETURN:
    {
        uint count = BytecodeReadUint(&bytecode);
        assert(count > 0);
        fputs("return {", stdout);
        printValue(&bytecode);
        while (--count)
        {
            fputs(",", stdout);
            printValue(&bytecode);
        }
        puts("}");
        break;
    }

    case OP_RETURN_VOID:
        puts("return");
        break;

    case OP_INVOKE:
    {
        functionref function = BytecodeReadRef(&bytecode);
        uint count = BytecodeReadUint(&bytecode);
        printf("invoke %s(", HeapGetString(FunctionIndexGetName(function)));
        if (count)
        {
            printValue(&bytecode);
            while (--count)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
        }
        count = *bytecode++;
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

    case OP_INVOKE_NATIVE:
    {
        nativefunctionref nativeFunction = refFromUint(*bytecode++);
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
