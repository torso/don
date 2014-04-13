#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "file.h"
#include "interpreter.h"
#include "linker.h"
#include "log.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"
#include "work.h"

static const boolean TRACE = false;

static const int *vmBytecode;

static VM **vmTable;
static uint vmTableSize = 16;
static uint vmCount;


static void addVM(VM *vm)
{
    VM **newTable;

    if (vmCount == vmTableSize)
    {
        vmTableSize *= 2;
        newTable = (VM**)malloc(vmTableSize * sizeof(*vmTable));
        memcpy(newTable, vmTable, vmCount * sizeof(*vmTable));
        free(vmTable);
        vmTable = newTable;
    }
    vmTable[vmCount++] = vm;
}

static void removeVM(uint index)
{
    VMDispose(vmTable[index]);
    vmTable[index] = vmTable[--vmCount];
}

static vref loadValue(VM *vm, int bp, int variable)
{
    if (variable >= 0)
    {
        return IVGetRef(&vm->stack, (size_t)(bp + variable));
    }
    if (-variable <= vm->constantCount)
    {
        return vm->constants[-variable - 1];
    }
    return vm->fields[-variable - vm->constantCount - 1];
}

static void storeValue(VM *vm, int bp, int variable, vref value)
{
    if (variable >= 0)
    {
        IVSetRef(&vm->stack, (size_t)(bp + variable), value);
        return;
    }
    assert(-variable > vm->constantCount);
    vm->fields[-variable - vm->constantCount - 1] = value;
}


static void initStackFrame(VM *vm, const int **ip, int *bp, int functionOffset,
                           uint parameterCount)
{
    const int *bytecode = vmBytecode + functionOffset;
    int i = *bytecode++;
    int localsCount = i >> 8;
    if (TRACE)
    {
        printf("[%p] %u: ", (void*)vm, functionOffset);
        BytecodeDisassembleInstruction(vmBytecode + functionOffset, vmBytecode);
    }
    assert((i & 0xff) == OP_FUNCTION);
    *ip = bytecode;
    *bp = (int)(IVSize(&vm->stack) - parameterCount);
    IVSetSize(&vm->stack, (size_t)(*bp + localsCount));
}

static void popStackFrame(VM *vm, const int **ip, int *bp, uint returnValues)
{
    uint expectedReturnValues;
    const int *oldIP = *ip;
    int oldBP = *bp;

    *bp = IVPop(&vm->callStack);
    *ip = vmBytecode + IVPop(&vm->callStack);

    expectedReturnValues = (uint)*(*ip)++;
    assert(returnValues >= expectedReturnValues); /* TODO: Fail nicely */
    while (expectedReturnValues--)
    {
        storeValue(vm, *bp, *(*ip)++, loadValue(vm, oldBP, *oldIP++));
    }

    IVSetSize(&vm->stack, (size_t)oldBP);
}


static void execute(VM *vm)
{
    const int *ip = vm->ip;
    vref value;
    vref string;
    int function;
    nativefunctionref nativeFunction;
    boolean condition;

    for (;;)
    {
        int i = *ip;
        int arg = i >> 8;
        if (TRACE)
        {
            printf("[%p] %u: ", (void*)vm, (uint)(ip - vmBytecode));
            BytecodeDisassembleInstruction(ip, vmBytecode);
        }
        ip++;
        switch ((Instruction)(i & 0xff))
        {
        case OP_NULL:
            storeValue(vm, vm->bp, arg, 0);
            break;

        case OP_TRUE:
            storeValue(vm, vm->bp, arg, HeapTrue);
            break;

        case OP_FALSE:
            storeValue(vm, vm->bp, arg, HeapFalse);
            break;

        case OP_EMPTY_LIST:
            storeValue(vm, vm->bp, arg, HeapEmptyList);
            break;

        case OP_LIST:
        {
            vref *array;
            vref *write;
            assert(arg);
            array = HeapCreateArray((size_t)arg);
            for (write = array; arg--; write++)
            {
                *write = loadValue(vm, vm->bp, *ip++);
            }
            storeValue(vm, vm->bp, *ip++, HeapFinishArray(array));
            break;
        }

        case OP_FILELIST:
            string = refFromInt(arg);
            storeValue(vm, vm->bp, *ip++,
                       HeapCreateFilelistGlob(HeapGetString(string), VStringLength(string)));
            break;

        case OP_STORE_CONSTANT:
            storeValue(vm, vm->bp, arg, refFromInt(*ip++));
            break;

        case OP_COPY:
            storeValue(vm, vm->bp, *ip++, loadValue(vm, vm->bp, arg));
            break;

        case OP_NOT:
        case OP_NEG:
        case OP_INV:
            storeValue(vm, vm->bp, *ip++,
                       HeapApplyUnary((Instruction)(i & 0xff),
                                      loadValue(vm, vm->bp, arg)));
            break;

        case OP_ITER_GET:
            value = loadValue(vm, vm->bp, *ip++);
            condition = HeapCollectionGet(loadValue(vm, vm->bp, arg), value, &value);
            storeValue(vm, vm->bp, *ip++, value);
            storeValue(vm, vm->bp, *ip++, condition ? HeapTrue : HeapFalse);
            break;

        case OP_EQUALS:
        case OP_NOT_EQUALS:
        case OP_LESS_EQUALS:
        case OP_GREATER_EQUALS:
        case OP_LESS:
        case OP_GREATER:
        case OP_AND:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_REM:
        case OP_CONCAT_LIST:
        case OP_CONCAT_STRING:
        case OP_INDEXED_ACCESS:
        case OP_RANGE:
            value = loadValue(vm, vm->bp, *ip++);
            storeValue(vm, vm->bp, *ip++,
                       HeapApplyBinary((Instruction)(i & 0xff), value,
                                       loadValue(vm, vm->bp, arg)));
            break;

        case OP_JUMP:
            vm->ip = ip + arg + 1;
            return;

        case OP_BRANCH_TRUE:
            value = loadValue(vm, vm->bp, *ip++);
            switch (VGetBool(value))
            {
            case TRUTHY:
                ip += arg;
                break;
            case FALSY:
                break;
            case FUTURE:
                addVM(VMClone(vm, value, ip + arg));
                break;
            }
            vm->ip = ip;
            return;

        case OP_BRANCH_FALSE:
            value = loadValue(vm, vm->bp, *ip++);
            switch (VGetBool(value))
            {
            case TRUTHY:
                break;
            case FALSY:
                ip += arg;
                break;
            case FUTURE:
                addVM(VMClone(vm, value, ip + arg));
                break;
            }
            vm->ip = ip;
            return;

        case OP_RETURN:
            assert(IVSize(&vm->callStack));
            popStackFrame(vm, &ip, &vm->bp, (uint)arg);
            break;

        case OP_RETURN_VOID:
            if (!IVSize(&vm->callStack))
            {
                vm->ip = null;
                return;
            }
            popStackFrame(vm, &ip, &vm->bp, 0);
            break;

        case OP_INVOKE:
        {
            vref *values;
            function = *ip++;
            IVReserveAppendSize(&vm->stack, (size_t)arg);
            values = (vref*)IVGetAppendPointer(&vm->stack);
            for (i = 0; i < arg; i++)
            {
                *values++ = loadValue(vm, vm->bp, *ip++);
            }
            IVGrow(&vm->stack, (size_t)arg);
            IVAdd(&vm->callStack, (int)(ip - vmBytecode));
            IVAdd(&vm->callStack, vm->bp);
            initStackFrame(vm, &ip, &vm->bp, function, (uint)arg);
            vm->ip = ip;
            break;
        }

        case OP_INVOKE_NATIVE:
            nativeFunction = refFromInt(arg);
            vm->ip = ip;
            NativeInvoke(vm, nativeFunction);
            return;

        case OP_FUNCTION:
        case OP_FUNCTION_UNLINKED:
        case OP_JUMPTARGET:
        case OP_JUMP_INDEXED:
        case OP_BRANCH_TRUE_INDEXED:
        case OP_BRANCH_FALSE_INDEXED:
        case OP_INVOKE_UNLINKED:
        case OP_UNKNOWN_VALUE:
        case OP_FILE:
        case OP_LINE:
        case OP_ERROR:
        default:
            unreachable;
        }
    }
}

void InterpreterExecute(const LinkedProgram *program, int target)
{
    VM *vm;
    uint i;
    boolean idle = false;

    vmBytecode = program->bytecode;
    vmTable = (VM**)malloc(vmTableSize * sizeof(*vmTable));
    vm = VMCreate(program);
    addVM(vm);
    initStackFrame(vm, &vm->ip, &vm->bp, target, 0);

    do
    {
        if (idle)
        {
            assert(!WorkQueueEmpty());
            WorkExecute();
        }

        idle = true;
        for (i = 0; i < vmCount; i++)
        {
            vm = vmTable[i];
            vm->condition = HeapTryWait(vm->condition);
            if (VIsFalsy(vm->condition))
            {
                removeVM(i--);
            }
            else if (vm->ip)
            {
                execute(vm);
                idle = false;
            }
        }
    }
    while (vmCount > 1 || vmTable[0]->ip);

    while (!WorkQueueEmpty())
    {
        WorkExecute();
    }

    removeVM(0);
    free(vmTable);
}
