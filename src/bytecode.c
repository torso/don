#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include "bytecode.h"
#include "bytevector.h"
#include "heap.h"
#include "namespace.h"
#include "native.h"
#include "stringpool.h"

#define MAX(a, b) (a > b ? a : b)

static void printValue(const int **bytecode)
{
    printf("#%d", *(*bytecode)++);
}

static void printBinaryOperation(const int **bytecode, const char *op, int arg)
{
    int r1 = *(*bytecode)++;
    int r2 = *(*bytecode)++;
    printf("#%d %s #%d -> #%d\n", arg, op, r1, r2);
}

static const int *disassemble(const int *bytecode, const int *base)
{
    int ip = (int)(bytecode - base);
    int i = *bytecode++;
    int arg = i >> 8;

    switch ((Instruction)(i & 0xff))
    {
    case OP_FILE:
    {
        vref ns = NamespaceGetName(refFromInt(arg));
        int length = *bytecode++;
        printf("file %s namespace:%s\n", (const char*)bytecode,
               ns ? HeapGetString(ns) : "<unnamed>");
        bytecode += (length + 4) >> 2;
        break;
    }

    case OP_LINE:
        printf("line %u\n", arg);
        break;

    case OP_ERROR:
        printf("error: %s\n", HeapGetString(refFromInt(arg)));
        break;

    case OP_FUNCTION:
        printf("function locals:%d\n", arg);
        break;

    case OP_FUNCTION_UNLINKED:
    {
        int parameterCount = *bytecode++;
        int vararg = *bytecode++;
        int param;
        printf("function %s parameters:%u(",
               arg ? HeapGetString(refFromInt(arg)) : "unknown",
               parameterCount);
        assert(parameterCount >= 0);
        for (param = 0; param < parameterCount; param++)
        {
            const char *name = HeapGetString(refFromInt(*bytecode++));
            int value = *bytecode++;
            printf("%s", name);
            if (param == vararg)
            {
                fputs("...", stdout);
            }
            if (value != INT_MAX)
            {
                printf("=#%d", value);
            }
            if (param + 1 < parameterCount)
            {
                fputs(",", stdout);
            }
        }
        fputs(")\n", stdout);
        break;
    }

    case OP_NULL:
        printf("store_null -> #%d\n", arg);
        break;

    case OP_TRUE:
        printf("store_true -> #%d\n", arg);
        break;

    case OP_FALSE:
        printf("store_false -> #%d\n", arg);
        break;

    case OP_EMPTY_LIST:
        printf("store_{} -> #%d\n", arg);
        break;

    case OP_LIST:
        printf("new list %u {", arg);
        assert(arg > 0);
        printValue(&bytecode);
        while (--arg)
        {
            fputs(",", stdout);
            printValue(&bytecode);
        }
        fputs("} -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_FILELIST:
        printf("filelist %s -> ", HeapGetString(refFromInt(arg)));
        printValue(&bytecode);
        puts("");
        break;

    case OP_STORE_CONSTANT:
    {
        char *string = HeapDebug(refFromInt(*bytecode++), false);
        printf("store_constant %s -> #%d\n", string, arg);
        free(string);
        break;
    }

    case OP_COPY:
        printf("copy #%d -> #%d\n", arg, *bytecode++);
        break;

    case OP_LOAD_FIELD:
    {
        namespaceref ns = refFromInt(*bytecode++);
        printf("load_field %s.%s -> #%d\n", HeapGetString(ns),
               HeapGetString(refFromInt(arg)), *bytecode++);
        break;
    }

    case OP_STORE_FIELD:
    {
        namespaceref ns = refFromInt(*bytecode++);
        printf("store_field #%d -> %s.%s\n", *bytecode++, HeapGetString(ns),
               HeapGetString(refFromInt(arg)));
        break;
    }

    case OP_NOT:
        printf("not #%d -> #%d\n", arg, *bytecode++);
        break;

    case OP_NEG:
        printf("neg #%d -> #%d\n", arg, *bytecode++);
        break;

    case OP_INV:
        printf("inv #%d -> #%d\n", arg, *bytecode++);
        break;

    case OP_ITER_GET:
        printf("iter_get #%d[", arg);
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
        fputs("concat_string ", stdout);
        assert(arg > 0);
        printValue(&bytecode);
        while (--arg)
        {
            fputs(",", stdout);
            printValue(&bytecode);
        }
        fputs(" -> ", stdout);
        printValue(&bytecode);
        puts("");
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
        printf("indexed_access #%d[", arg);
        printValue(&bytecode);
        fputs("] -> ", stdout);
        printValue(&bytecode);
        puts("");
        break;

    case OP_RANGE:
        printBinaryOperation(&bytecode, "..", arg);
        break;

    case OP_JUMPTARGET:
        printf("jump_target %u\n", arg);
        break;

    case OP_JUMP:
        printf("jump %d\n", ip + 2 + arg);
        break;

    case OP_JUMP_INDEXED:
    {
        printf("jump_indexed %u\n", arg);
        break;
    }

    case OP_BRANCH_TRUE:
    {
        int value = *bytecode++;
        printf("branch_true #%d, %u\n", value, ip + 2 + arg);
        break;
    }

    case OP_BRANCH_TRUE_INDEXED:
    {
        int value = *bytecode++;
        printf("branch_true_indexed #%d, %u\n", value, arg);
        break;
    }

    case OP_BRANCH_FALSE:
    {
        int value = *bytecode++;
        printf("branch_false #%d, %u\n", value, ip + 2 + arg);
        break;
    }

    case OP_BRANCH_FALSE_INDEXED:
    {
        int value = *bytecode++;
        printf("branch_false_indexed #%d, %u\n", value, arg);
        break;
    }

    case OP_RETURN:
        fputs("return ", stdout);
        assert(arg > 0);
        printValue(&bytecode);
        while (--arg)
        {
            fputs(",", stdout);
            printValue(&bytecode);
        }
        puts("");
        break;

    case OP_RETURN_VOID:
        puts("return");
        break;

    case OP_INVOKE:
    {
        int returnCount;
        printf("invoke %u(", *bytecode++);
        assert(arg >= 0);
        if (arg)
        {
            printValue(&bytecode);
            while (--arg)
            {
                fputs(",", stdout);
                printValue(&bytecode);
            }
        }
        returnCount = *bytecode++;
        assert(returnCount >= 0);
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

    case OP_INVOKE_UNLINKED:
    {
        vref functionName = refFromInt(arg);
        vref ns = refFromInt(*bytecode++);
        int argumentCount = *bytecode++;
        int returnCount = *bytecode++;

        fputs("invoke_unlinked ", stdout);
        if (ns)
        {
            printf("%s.", HeapGetString(ns));
        }
        printf("%s(", HeapGetString(functionName));
        assert(argumentCount >= 0);
        assert(returnCount >= 0);
        while (argumentCount--)
        {
            if (*bytecode)
            {
                printf("%s:", HeapGetString(refFromInt(*bytecode)));
            }
            bytecode++;
            printValue(&bytecode);
            if (argumentCount)
            {
                fputs(",", stdout);
            }
        }
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
        nativefunctionref nativeFunction = refFromInt(arg);
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

    default:
        puts("unknown opcode");
        break;
    }
    return bytecode;
}

const int *BytecodeDisassembleInstruction(const int *bytecode, const int *base)
{
    return disassemble(bytecode, base);
}

void BytecodeDisassemble(const int *bytecode, const int *bytecodeLimit)
{
    const int *start = bytecode;

    while (bytecode < bytecodeLimit)
    {
        printf(" %u: ", (uint)(bytecode - start));
        bytecode = disassemble(bytecode, start);
    }
}
