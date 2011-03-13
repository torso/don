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
        newTable = (VM**)malloc(vmTableSize * sizeof(vmTable[0]));
        memcpy(newTable, vmTable, vmCount * sizeof(vmTable[0]));
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

static objectref getLocal(VM *vm, uint bp, uint16 local)
{
    return IntVectorGetRef(&vm->stack, bp + local);
}

static void storeLocal(VM *vm, uint bp, uint16 local, objectref value)
{
    IntVectorSetRef(&vm->stack, bp + local, value);
}


static void pushStackFrame(VM *vm, const byte **ip, uint *bp,
                           functionref function, uint returnValues)
{
    uint localsCount;
    IntVectorAdd(&vm->callStack, (uint)(*ip - vmBytecode));
    IntVectorAdd(&vm->callStack, *bp);
    IntVectorAdd(&vm->callStack, returnValues);
    *ip = vmBytecode + FunctionIndexGetBytecodeOffset(function);
    *bp = (uint)IntVectorSize(&vm->stack) -
        FunctionIndexGetParameterCount(function);
    localsCount = FunctionIndexGetLocalsCount(function);
    IntVectorSetSize(&vm->stack, *bp + localsCount);
}

static void popStackFrame(VM *vm, const byte **ip, uint *bp,
                          uint returnValues)
{
    uint expectedReturnValues = IntVectorPop(&vm->callStack);

    IntVectorCopy(&vm->stack,
                  IntVectorSize(&vm->stack) - returnValues,
                  &vm->stack,
                  *bp,
                  expectedReturnValues);
    IntVectorSetSize(&vm->stack, *bp + expectedReturnValues);

    *bp = IntVectorPop(&vm->callStack);
    *ip = vmBytecode + IntVectorPop(&vm->callStack);
}


static void execute(VM *vm)
{
    Instruction op;
    const byte *ip = vm->ip;
    uint argumentCount;
    uint returnValueCount;
    int jumpOffset;
    objectref value;
    objectref value2;
    size_t size1;
    size_t size2;
    stringref string;
    byte *objectData;
    Iterator *piter;
    functionref function;
    nativefunctionref nativeFunction;

    for (;;)
    {
        if (TRACE)
        {
            BytecodeDisassembleInstruction(ip, vmBytecode);
        }
        op = (Instruction)*ip++;
        switch (op)
        {
        case OP_NULL:
            VMPush(vm, 0);
            break;

        case OP_TRUE:
            VMPush(vm, HeapTrue);
            break;

        case OP_FALSE:
            VMPush(vm, HeapFalse);
            break;

        case OP_INTEGER:
            VMPush(vm, HeapBoxInteger(BytecodeReadInt(&ip)));
            break;

        case OP_STRING:
            VMPush(vm, HeapCreatePooledString(BytecodeReadRef(&ip)));
            break;

        case OP_EMPTY_LIST:
            VMPush(vm, HeapEmptyList);
            break;

        case OP_LIST:
            size1 = BytecodeReadUint(&ip);
            assert(size1);
            objectData = HeapAlloc(TYPE_ARRAY, size1 * sizeof(objectref));
            objectData += size1 * sizeof(objectref);
            while (size1--)
            {
                objectData -= sizeof(objectref);
                *(objectref*)objectData = VMPop(vm);
            }
            VMPush(vm, HeapFinishAlloc(objectData));
            break;

        case OP_FILE:
            string = BytecodeReadRef(&ip);
            VMPush(vm, HeapCreateFile(
                       FileAdd(StringPoolGetString(string),
                               StringPoolGetStringLength(string))));
            break;

        case OP_FILESET:
            VMPush(vm, HeapCreateFilesetGlob(
                       StringPoolGetString(BytecodeReadRef(&ip))));
            break;

        case OP_POP:
            VMPop(vm);
            break;

        case OP_DUP:
            VMPush(vm, VMPeek(vm));
            break;

        case OP_REORDER_STACK:
            value = BytecodeReadUint16(&ip);
            size2 = IntVectorSize(&vm->stack);
            size1 = size2 - value;
            IntVectorGrow(&vm->stack, value);
            IntVectorMove(&vm->stack, size1, size2, value);
            while (value--)
            {
                IntVectorSet(&vm->stack, size1++,
                             IntVectorGet(
                                 &vm->stack,
                                 size2 + BytecodeReadUint16(&ip)));
            }
            IntVectorSetSize(&vm->stack, size2);
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
        case OP_ITER_INIT:
            VMPush(vm, HeapApplyUnary(op, VMPop(vm)));
            break;

        case OP_ITER_NEXT:
            value = HeapWait(VMPop(vm));
            assert(HeapGetObjectType(value) == TYPE_ITERATOR);
            assert(HeapGetObjectSize(value) == sizeof(Iterator));
            piter = (Iterator*)HeapGetObjectData(value);
            value = 0;
            VMPushBoolean(vm, HeapIteratorNext(piter, &value));
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
            value = HeapTryWait(VMPop(vm));
            if (HeapIsFutureValue(value))
            {
                addVM(VMClone(vm, value, ip + jumpOffset));
                VMApplyCondition(vm, HeapApplyUnary(OP_NOT, value));
            }
            else
            {
                assert(value == HeapTrue || value == HeapFalse);
                if (value == HeapTrue)
                {
                    ip += jumpOffset;
                }
            }
            vm->ip = ip;
            return;

        case OP_BRANCH_FALSE:
            jumpOffset = BytecodeReadInt(&ip);
            value = HeapTryWait(VMPop(vm));
            if (HeapIsFutureValue(value))
            {
                addVM(VMClone(vm, HeapApplyUnary(OP_NOT, value),
                              ip + jumpOffset));
                VMApplyCondition(vm, value);
            }
            else
            {
                assert(value == HeapTrue || value == HeapFalse);
                if (value != HeapTrue)
                {
                    ip += jumpOffset;
                }
            }
            vm->ip = ip;
            return;

        case OP_RETURN:
            assert(IntVectorSize(&vm->callStack));
            popStackFrame(vm, &ip, &vm->bp, *ip++);
            break;

        case OP_RETURN_VOID:
            if (!IntVectorSize(&vm->callStack))
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
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == FunctionIndexGetParameterCount(function));
            returnValueCount = *ip++;
            pushStackFrame(vm, &ip, &vm->bp, function, returnValueCount);
            vm->ip = ip;
            return;

        case OP_INVOKE_NATIVE:
            nativeFunction = refFromUint(*ip++);
            vm->ip = ip;
            NativeInvoke(vm, nativeFunction);
            return;

        case OP_UNKNOWN_VALUE:
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
    vmTable = (VM**)malloc(vmTableSize * sizeof(vmTable[0]));
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
            if (vm->condition == HeapFalse)
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
