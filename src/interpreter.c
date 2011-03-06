#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "instruction.h"
#include "interpreter.h"
#include "log.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"
#include "work.h"

static const boolean TRACE = false;


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


static void execute(VM *vm, functionref target)
{
    const byte *ip = vmBytecode + FunctionIndexGetBytecodeOffset(target);
    const byte *baseIP = ip;
    uint bp = 0;
    uint argumentCount;
    uint returnValueCount;
    int jumpOffset;
    uint local;
    objectref value;
    objectref value2;
    size_t size1;
    size_t size2;
    stringref string;
    byte *objectData;
    Iterator *piter;
    functionref function;
    nativefunctionref nativeFunction;

    local = FunctionIndexGetLocalsCount(target);
    IntVectorSetSize(&vm->stack, local);
    for (;;)
    {
        if (TRACE)
        {
            BytecodeDisassembleInstruction(ip, baseIP);
        }
        switch ((Instruction)*ip++)
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
            VMPush(vm, getLocal(vm, bp, BytecodeReadUint16(&ip)));
            break;

        case OP_STORE:
            storeLocal(vm, bp, BytecodeReadUint16(&ip), VMPop(vm));
            break;

        case OP_LOAD_FIELD:
            VMPush(vm, vm->fields[BytecodeReadUint(&ip)]);
            break;

        case OP_STORE_FIELD:
            vm->fields[BytecodeReadUint(&ip)] = VMPop(vm);
            break;

        case OP_CAST_BOOLEAN:
            VMPushBoolean(vm, VMPopBoolean(vm));
            break;

        case OP_EQUALS:
            VMPushBoolean(vm, HeapEquals(VMPop(vm), VMPop(vm)));
            break;

        case OP_NOT_EQUALS:
            VMPushBoolean(vm, !HeapEquals(VMPop(vm), VMPop(vm)));
            break;

        case OP_LESS_EQUALS:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPushBoolean(vm, HeapCompare(value2, value) <= 0);
            break;

        case OP_GREATER_EQUALS:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPushBoolean(vm, HeapCompare(value2, value) >= 0);
            break;

        case OP_LESS:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPushBoolean(vm, HeapCompare(value2, value) < 0);
            break;

        case OP_GREATER:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPushBoolean(vm, HeapCompare(value2, value) > 0);
            break;

        case OP_NOT:
            value = VMPop(vm);
            assert(value == HeapTrue || value == HeapFalse);
            VMPushBoolean(vm, value == HeapFalse);
            break;

        case OP_NEG:
            assert(HeapUnboxInteger(VMPeek(vm)) != INT_MIN);
            VMPush(vm, HeapBoxInteger(-HeapUnboxInteger(VMPop(vm))));
            break;

        case OP_INV:
            VMPush(vm, HeapBoxInteger(~HeapUnboxInteger(VMPop(vm))));
            break;

        case OP_ADD:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPush(vm, HeapBoxInteger(HeapUnboxInteger(value2) +
                                      HeapUnboxInteger(value)));
            break;

        case OP_SUB:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPush(vm, HeapBoxInteger(HeapUnboxInteger(value2) -
                                      HeapUnboxInteger(value)));
            break;

        case OP_MUL:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPush(vm, HeapBoxInteger(HeapUnboxInteger(value2) *
                                      HeapUnboxInteger(value)));
            break;

        case OP_DIV:
            value = VMPop(vm);
            value2 = VMPop(vm);
            assert((HeapUnboxInteger(value2) /
                    HeapUnboxInteger(value)) *
                   HeapUnboxInteger(value) ==
                   HeapUnboxInteger(value2)); /* TODO: fraction */
            VMPush(vm, HeapBoxInteger(HeapUnboxInteger(value2) /
                                      HeapUnboxInteger(value)));
            break;

        case OP_REM:
            value = VMPop(vm);
            value2 = VMPop(vm);
            VMPush(vm, HeapBoxInteger(HeapUnboxInteger(value2) %
                                      HeapUnboxInteger(value)));
            break;

        case OP_CONCAT_STRING:
            value = VMPop(vm);
            value2 = VMPop(vm);
            size1 = HeapStringLength(value2);
            size2 = HeapStringLength(value);
            if (!size1 && !size2)
            {
                VMPush(vm, HeapEmptyString);
                break;
            }
            objectData = HeapAlloc(TYPE_STRING, size1 + size2);
            HeapWriteString(value2, (char*)objectData);
            HeapWriteString(value, (char*)objectData + size1);
            VMPush(vm, HeapFinishAlloc(objectData));
            break;

        case OP_CONCAT_LIST:
            value = VMPop(vm);
            value2 = VMPop(vm);
            value = HeapConcatList(value2, value);
            VMPush(vm, value);
            break;

        case OP_INDEXED_ACCESS:
            value = VMPop(vm);
            value2 = VMPop(vm);
            if (HeapIsString(value2))
            {
                if (HeapIsRange(value))
                {
                    size1 = HeapUnboxSize(HeapRangeLow(value));
                    size2 = HeapUnboxSize(HeapRangeHigh(value));
                    assert(size2 >= size1); /* TODO: Support inverted ranges. */
                    value = HeapCreateSubstring(value2, size1, size2 - size1 + 1);
                }
                else
                {
                    value = HeapCreateSubstring(value2, HeapUnboxSize(value), 1);
                }
            }
            else
            {
                HeapCollectionGet(value2, value, &value);
            }
            VMPush(vm, value);
            break;

        case OP_RANGE:
            value = VMPop(vm);
            value2 = VMPop(vm);
            value = HeapCreateRange(value2, value);
            VMPush(vm, value);
            break;

        case OP_ITER_INIT:
            value = HeapCreateIterator(VMPop(vm));
            VMPush(vm, value);
            break;

        case OP_ITER_NEXT:
            value = VMPop(vm);
            assert(HeapGetObjectType(value) == TYPE_ITERATOR);
            assert(HeapGetObjectSize(value) == sizeof(Iterator));
            piter = (Iterator*)HeapGetObjectData(value);
            value = 0;
            VMPushBoolean(vm, HeapIteratorNext(piter, &value));
            VMPush(vm, value);
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            ip += jumpOffset;
            break;

        case OP_BRANCH_TRUE:
            assert(VMPeek(vm) == HeapTrue || VMPeek(vm) == HeapFalse);
            jumpOffset = BytecodeReadInt(&ip);
            if (VMPop(vm) == HeapTrue)
            {
                ip += jumpOffset;
            }
            break;

        case OP_BRANCH_FALSE:
            assert(VMPeek(vm) == HeapTrue || VMPeek(vm) == HeapFalse);
            jumpOffset = BytecodeReadInt(&ip);
            if (VMPop(vm) != HeapTrue)
            {
                ip += jumpOffset;
            }
            break;

        case OP_RETURN:
            assert(IntVectorSize(&vm->callStack));
            popStackFrame(vm, &ip, &bp, *ip++);
            baseIP = vmBytecode +
                FunctionIndexGetBytecodeOffset(
                    FunctionIndexGetFunctionFromBytecode(
                        (uint)(ip - vmBytecode)));
            break;

        case OP_RETURN_VOID:
            if (!IntVectorSize(&vm->callStack))
            {
                assert(IntVectorSize(&vm->stack) ==
                       FunctionIndexGetLocalsCount(target));
                return;
            }
            popStackFrame(vm, &ip, &bp, 0);
            baseIP = vmBytecode +
                FunctionIndexGetBytecodeOffset(
                    FunctionIndexGetFunctionFromBytecode(
                        (uint)(ip - vmBytecode)));
            break;

        case OP_INVOKE:
            function = BytecodeReadRef(&ip);
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == FunctionIndexGetParameterCount(function));
            returnValueCount = *ip++;
            pushStackFrame(vm, &ip, &bp, function, returnValueCount);
            baseIP = ip;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = refFromUint(*ip++);
            NativeInvoke(vm, nativeFunction);
            break;

        case OP_UNKNOWN_VALUE:
            assert(false);
            break;
        }
    }
}

void InterpreterExecute(functionref target)
{
    VM vm;

    memset(&vm, 0, sizeof(VM));
    vm.fields = (objectref*)malloc(FieldIndexGetCount() * sizeof(int));
    IntVectorInit(&vm.callStack);
    IntVectorInit(&vm.stack);

    execute(&vm, FunctionIndexGetFirstFunction());
    execute(&vm, target);
    while (!WorkQueueEmpty())
    {
        WorkExecute();
    }

    free(vm.fields);
    vm.fields = null;
    IntVectorDispose(&vm.callStack);
    IntVectorDispose(&vm.stack);
}
