#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "interpreter.h"
#include "log.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"
#include "work.h"

static const boolean TRACE = false;

static const byte *vmBytecode;

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

static vref getLocal(VM *vm, uint bp, uint local)
{
    return IVGetRef(&vm->stack, bp + local);
}

static void storeLocal(VM *vm, uint bp, uint local, vref value)
{
    IVSetRef(&vm->stack, bp + local, value);
}


static void initStackFrame(VM *vm, const byte **ip, uint *bp, uint functionOffset)
{
    vref function;
    uint parameterCount;
    uint localsCount;
    *ip = vmBytecode + functionOffset;
    assert(**ip == OP_FUNCTION);
    (*ip)++;
    function = BytecodeReadRef(ip);
    parameterCount = BytecodeReadUint(ip);
    localsCount = BytecodeReadUint(ip);
    if (function)
    {
        assert(parameterCount == FunctionIndexGetParameterCount(function));
        assert(localsCount == FunctionIndexGetLocalsCount(function));
    }
    *bp = (uint)IVSize(&vm->stack) - parameterCount;
    IVSetSize(&vm->stack, *bp + localsCount);
}

static void popStackFrame(VM *vm, const byte **ip, uint *bp, uint returnValues)
{
    uint expectedReturnValues;
    const byte *oldIP = *ip;
    uint oldBP = *bp;

    *bp = IVPop(&vm->callStack);
    *ip = vmBytecode + IVPop(&vm->callStack);

    expectedReturnValues = *(*ip)++;
    assert(returnValues >= expectedReturnValues); /* TODO: Fail nicely */
    while (expectedReturnValues--)
    {
        storeLocal(vm, *bp, BytecodeReadUint(ip), getLocal(vm, oldBP, BytecodeReadUint(&oldIP)));
    }

    IVSetSize(&vm->stack, oldBP);
}


static void execute(VM *vm)
{
    Instruction op;
    const byte *ip = vm->ip;
    uint argumentCount;
    uint i;
    int jumpOffset;
    vref value;
    vref value2;
    size_t size;
    vref string;
    byte *objectData;
    functionref function;
    nativefunctionref nativeFunction;
    boolean condition;

    for (;;)
    {
        if (TRACE)
        {
            printf("[%p] %u: ", (void*)vm, (uint)(ip - vmBytecode));
            BytecodeDisassembleInstruction(ip, vmBytecode);
        }
        op = (Instruction)*ip++;
        switch (op)
        {
        case OP_FUNCTION:
            assert(false);
            break;

        case OP_NULL:
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), 0);
            break;

        case OP_TRUE:
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), HeapTrue);
            break;

        case OP_FALSE:
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), HeapFalse);
            break;

        case OP_EMPTY_LIST:
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), HeapEmptyList);
            break;

        case OP_LIST:
        {
            vref *values;
            size = BytecodeReadUint(&ip);
            assert(size);
            objectData = HeapAlloc(TYPE_ARRAY, size * sizeof(vref));
            for (values = (vref*)objectData; size--; values++)
            {
                *values = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            }
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), HeapFinishAlloc(objectData));
            break;
        }

        case OP_FILELIST:
            string = BytecodeReadRef(&ip);
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip),
                       HeapCreateFilelistGlob(HeapGetString(string), VStringLength(string)));
            break;

        case OP_PUSH:
            value = BytecodeReadRef(&ip);
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), value);
            break;

        case OP_COPY:
            value = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), value);
            break;

        case OP_LOAD_FIELD:
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), vm->fields[BytecodeReadUint(&ip)]);
            break;

        case OP_STORE_FIELD:
            i = BytecodeReadUint(&ip);
            vm->fields[i] = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            break;

        case OP_NOT:
        case OP_NEG:
        case OP_INV:
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip),
                       HeapApplyUnary(op, getLocal(vm, vm->bp, BytecodeReadUint(&ip))));
            break;

        case OP_ITER_GET:
            value2 = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            value = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            condition = HeapCollectionGet(value2, value, &value);
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), value);
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), condition ? HeapTrue : HeapFalse);
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
            value2 = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            value = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            storeLocal(vm, vm->bp, BytecodeReadUint(&ip), HeapApplyBinary(op, value, value2));
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            vm->ip = ip + jumpOffset;
            return;

        case OP_BRANCH_TRUE:
            value = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            jumpOffset = BytecodeReadInt(&ip);
            switch (VGetBool(value))
            {
            case TRUTHY:
                ip += jumpOffset;
                break;
            case FALSY:
                break;
            case FUTURE:
                addVM(VMClone(vm, value, ip + jumpOffset));
                break;
            }
            vm->ip = ip;
            return;

        case OP_BRANCH_FALSE:
            value = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            jumpOffset = BytecodeReadInt(&ip);
            switch (VGetBool(value))
            {
            case TRUTHY:
                break;
            case FALSY:
                ip += jumpOffset;
                break;
            case FUTURE:
                addVM(VMClone(vm, value, ip + jumpOffset));
                break;
            }
            vm->ip = ip;
            return;

        case OP_RETURN:
            assert(IVSize(&vm->callStack));
            popStackFrame(vm, &ip, &vm->bp, BytecodeReadUint(&ip));
            break;

        case OP_RETURN_VOID:
            if (!IVSize(&vm->callStack))
            {
                if (vm->target)
                {
                    ip--;
                    IVSetSize(&vm->stack, 0);
                    initStackFrame(vm, &ip, &vm->bp, FunctionIndexGetBytecodeOffset(vm->target));
                    vm->target = 0;
                    break;
                }
                vm->ip = null;
                return;
            }
            popStackFrame(vm, &ip, &vm->bp, 0);
            break;

        case OP_INVOKE:
        {
            uint *values;
            function = BytecodeReadRef(&ip);
            argumentCount = BytecodeReadUint(&ip);
            IVReserveAppendSize(&vm->stack, argumentCount);
            values = IVGetAppendPointer(&vm->stack);
            for (i = 0; i < argumentCount; i++)
            {
                *values++ = getLocal(vm, vm->bp, BytecodeReadUint(&ip));
            }
            IVGrow(&vm->stack, argumentCount);
            IVAdd(&vm->callStack, (uint)(ip - vmBytecode));
            IVAdd(&vm->callStack, vm->bp);
            initStackFrame(vm, &ip, &vm->bp, FunctionIndexGetBytecodeOffset(function));
            vm->ip = ip;
            break;
        }

        case OP_INVOKE_NATIVE:
            nativeFunction = refFromUint(*ip++);
            vm->ip = ip;
            NativeInvoke(vm, nativeFunction);
            return;

        case OP_UNKNOWN_VALUE:
        default:
            assert(false);
            break;
        }
    }
}

void InterpreterExecute(const byte *bytecode, functionref target)
{
    VM *vm;
    uint i;
    boolean idle = false;

    vmBytecode = bytecode;
    vmTable = (VM**)malloc(vmTableSize * sizeof(*vmTable));
    vm = VMCreate(bytecode, target);
    addVM(vm);
    initStackFrame(vm, &vm->ip, &vm->bp, 0);

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
