#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "bytecode.h"
#include "linker.h"
#include "work.h"

const int *vmBytecode;
static const int *vmLineNumbers;

static VM *VMAlloc(int fieldCount)
{
    byte *data = (byte*)calloc(
        sizeof(VM) + (uint)fieldCount * sizeof(vref), 1);
    VM *vm = (VM*)data;
    vm->fields = (vref*)(data + sizeof(VM));
    vm->fieldCount = fieldCount;
    IVInit(&vm->callStack, 128);
    IVInit(&vm->stack, 1024);
    return vm;
}

VM *VMCreate(const LinkedProgram *program)
{
    VM *vm = VMAlloc(program->fieldCount);
    vmBytecode = program->bytecode;
    vmLineNumbers = program->lineNumbers;
    vm->parent = null;
    vm->condition = HeapTrue;
    vm->constants = program->constants;
    vm->constantCount = program->constantCount;
    vm->bp = 0;
    memcpy(vm->fields, program->fields, (uint)vm->fieldCount * sizeof(*vm->fields));
    return vm;
}

VM *VMClone(VM *vm, vref condition, const int *ip)
{
    VM *clone = VMAlloc(vm->fieldCount);
    VMBranch *parent = (VMBranch*)malloc(sizeof(VMBranch));

    parent->parent = vm->parent;
    parent->condition = vm->condition;
    parent->childCount = 2;

    vm->parent = parent;
    clone->parent = parent;
    clone->constants = vm->constants;
    clone->constantCount = vm->constantCount;
    memcpy(clone->fields, vm->fields, (uint)vm->fieldCount * sizeof(*vm->fields));
    clone->fieldCount = vm->fieldCount;
    IVAppendAll(&vm->callStack, &clone->callStack);
    IVAppendAll(&vm->stack, &clone->stack);
    clone->ip = ip;
    clone->bp = vm->bp;

    clone->condition = HeapApplyBinary(OP_AND, vm->condition, condition);
    vm->condition = HeapApplyBinary(OP_AND, vm->condition,
                                    HeapApplyUnary(OP_NOT, condition));

    return clone;
}

void VMDispose(VM *vm)
{
    VMBranch *parent = vm->parent;
    VMBranch *next;

    WorkDiscard(vm);
    while (parent)
    {
        if (--parent->childCount)
        {
            break;
        }
        next = parent->parent;
        free(parent);
        parent = next;
    }
    IVDispose(&vm->callStack);
    IVDispose(&vm->stack);
    free(vm);
}

void VMFail(VM *vm unused, const int *ip, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (ip)
    {
        const char *filename;
        int line = BytecodeLineNumber(vmLineNumbers, (size_t)(ip - vmBytecode), &filename);
        fprintf(stderr, "%s:%d: %s\n", filename, line,
                HeapGetStringCopy(HeapCreateStringFormatted(format, args)));
    }
    else
    {
        fprintf(stderr, "%s\n", HeapGetStringCopy(HeapCreateStringFormatted(format, args)));
    }
    va_end(args);
    cleanShutdown(EXIT_FAILURE);
}


vref VMReadValue(VM *vm)
{
    int variable = *vm->ip++;
    if (variable >= 0)
    {
        return refFromInt(IVGet(&vm->stack, (size_t)(vm->bp + variable)));
    }
    if (-variable <= vm->constantCount)
    {
        return vm->constants[-variable - 1];
    }
    return vm->fields[-variable - vm->constantCount - 1];
}

void VMStoreValue(VM *vm, int variable, vref value)
{
    if (variable >= 0)
    {
        IVSet(&vm->stack, (size_t)(vm->bp + variable), intFromRef(value));
        return;
    }
    assert(-variable > vm->constantCount);
    vm->fields[-variable - vm->constantCount - 1] = value;
}
