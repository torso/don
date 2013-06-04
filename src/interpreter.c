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

static vref getLocal(VM *vm, uint bp, uint16 local)
{
    return IVGetRef(&vm->stack, bp + local);
}

static void storeLocal(VM *vm, uint bp, uint16 local, vref value)
{
    IVSetRef(&vm->stack, bp + local, value);
}


static void pushStackFrame(VM *vm, const byte **ip, uint *bp,
                           functionref function, uint returnValues)
{
    uint localsCount;
    IVAdd(&vm->callStack, (uint)(*ip - vmBytecode));
    IVAdd(&vm->callStack, *bp);
    IVAdd(&vm->callStack, returnValues);
    *ip = vmBytecode + FunctionIndexGetBytecodeOffset(function);
    *bp = (uint)IVSize(&vm->stack) -
        FunctionIndexGetParameterCount(function);
    localsCount = FunctionIndexGetLocalsCount(function);
    IVSetSize(&vm->stack, *bp + localsCount);
}

static void popStackFrame(VM *vm, const byte **ip, uint *bp,
                          uint returnValues)
{
    uint expectedReturnValues = IVPop(&vm->callStack);

    IVCopy(&vm->stack,
           IVSize(&vm->stack) - returnValues,
           &vm->stack,
           *bp,
           expectedReturnValues);
    IVSetSize(&vm->stack, *bp + expectedReturnValues);

    *bp = IVPop(&vm->callStack);
    *ip = vmBytecode + IVPop(&vm->callStack);
}


static void execute(VM *vm)
{
    Instruction op;
    const byte *ip = vm->ip;
    uint argumentCount;
    uint returnValueCount;
    uint i;
    int jumpOffset;
    vref value;
    vref value2;
    size_t size;
    size_t src;
    size_t dst;
    vref string;
    byte *objectData;
    functionref function;
    nativefunctionref nativeFunction;

    for (;;)
    {
        if (TRACE)
        {
            printf("[%p]", (void*)vm);
            BytecodeDisassembleInstruction(ip, vmBytecode);
        }
        op = (Instruction)*ip++;
        switch (op)
        {
        case OP_PUSH:
            VMPush(vm, BytecodeReadUint(&ip));
            break;

        case OP_NULL:
            VMPush(vm, 0);
            break;

        case OP_TRUE:
            VMPush(vm, HeapTrue);
            break;

        case OP_FALSE:
            VMPush(vm, HeapFalse);
            break;

        case OP_EMPTY_LIST:
            VMPush(vm, HeapEmptyList);
            break;

        case OP_LIST:
            size = BytecodeReadUint(&ip);
            assert(size);
            objectData = HeapAlloc(TYPE_ARRAY, size * sizeof(vref));
            objectData += size * sizeof(vref);
            while (size--)
            {
                objectData -= sizeof(vref);
                *(vref*)objectData = VMPop(vm);
            }
            VMPush(vm, HeapFinishAlloc(objectData));
            break;

        case OP_FILELIST:
            string = BytecodeReadRef(&ip);
            VMPush(vm, HeapCreateFilelistGlob(HeapGetString(string), VStringLength(string)));
            break;

        case OP_POP:
            VMPop(vm);
            break;

        case OP_DUP:
            VMPush(vm, VMPeek(vm));
            break;

        case OP_REORDER_STACK:
            argumentCount = BytecodeReadUint16(&ip);
            src = IVSize(&vm->stack);
            dst = src - argumentCount;
            IVAppend(&vm->stack, dst, &vm->stack, argumentCount);
            for (i = 0; i < argumentCount; i++)
            {
                IVSet(&vm->stack, dst + i,
                      IVGet(&vm->stack, src + BytecodeReadUint16(&ip)));
            }
            IVSetSize(&vm->stack, src);
            break;

        case OP_LOAD:
            VMPush(vm, getLocal(vm, vm->bp, BytecodeReadUint16(&ip)));
            break;

        case OP_STORE:
            storeLocal(vm, vm->bp, BytecodeReadUint16(&ip), VMPop(vm));
            break;

        case OP_LOAD_FIELD:
            VMPush(vm, vm->fields[BytecodeReadUint(&ip)]);
            break;

        case OP_STORE_FIELD:
            vm->fields[BytecodeReadUint(&ip)] = VMPop(vm);
            break;

        case OP_CAST_BOOLEAN:
        case OP_NOT:
        case OP_NEG:
        case OP_INV:
            VMPush(vm, HeapApplyUnary(op, VMPop(vm)));
            break;

        case OP_ITER_GET:
            value = HeapWait(VMPop(vm));
            value2 = HeapWait(VMPop(vm));
            VMPushBoolean(vm, HeapCollectionGet(value2, value, &value));
            VMPush(vm, value);
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
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPush(vm, HeapApplyBinary(op, value, value2));
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            vm->ip = ip + jumpOffset;
            return;

        case OP_BRANCH_TRUE:
            jumpOffset = BytecodeReadInt(&ip);
            switch (VGetBool(VMPop(vm)))
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
            jumpOffset = BytecodeReadInt(&ip);
            switch (VGetBool(VMPop(vm)))
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
            popStackFrame(vm, &ip, &vm->bp, *ip++);
            break;

        case OP_RETURN_VOID:
            if (!IVSize(&vm->callStack))
            {
                if (vm->target)
                {
                    ip--;
                    pushStackFrame(vm, &ip, &vm->bp, vm->target, 0);
                    vm->target = 0;
                    break;
                }
                vm->ip = null;
                return;
            }
            popStackFrame(vm, &ip, &vm->bp, 0);
            break;

        case OP_INVOKE:
            function = BytecodeReadRef(&ip);
            returnValueCount = *ip++;
            pushStackFrame(vm, &ip, &vm->bp, function, returnValueCount);
            vm->ip = ip;
            break;

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
    addVM(VMCreate(bytecode, target));

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
