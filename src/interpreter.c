#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "cache.h"
#include "fieldindex.h"
#include "file.h"
#include "functionindex.h"
#include "hash.h"
#include "instruction.h"
#include "interpreter.h"
#include "log.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"

static const boolean TRACE = false;


#define peek InterpreterPeek
#define pop InterpreterPop
#define push InterpreterPush

objectref InterpreterPeek(VM *vm)
{
    return IntVectorPeekRef(&vm->stack);
}

objectref InterpreterPop(VM *vm)
{
    return IntVectorPopRef(&vm->stack);
}

boolean InterpreterPopBoolean(VM *vm)
{
    return HeapIsTrue(vm, IntVectorPopRef(&vm->stack));
}

void InterpreterPush(VM *vm, objectref value)
{
    IntVectorAddRef(&vm->stack, value);
}

static void pushBoolean(VM *vm, boolean value)
{
    if (value)
    {
        push(vm, vm->booleanTrue);
    }
    else
    {
        push(vm, vm->booleanFalse);
    }
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
    IntVectorAdd(&vm->callStack, (uint)(*ip - vm->bytecode));
    IntVectorAdd(&vm->callStack, *bp);
    IntVectorAdd(&vm->callStack, returnValues);
    *ip = vm->bytecode + FunctionIndexGetBytecodeOffset(function);
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
    *ip = vm->bytecode + IntVectorPop(&vm->callStack);
}


static void execute(VM *vm, functionref target)
{
    const byte *ip = vm->bytecode + FunctionIndexGetBytecodeOffset(target);
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
    Iterator iter;
    Iterator *piter;
    functionref function;
    nativefunctionref nativeFunction;
    fileref file;
    HashState hashState;
    byte hash[DIGEST_SIZE];

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
            push(vm, 0);
            break;

        case OP_TRUE:
            push(vm, vm->booleanTrue);
            break;

        case OP_FALSE:
            push(vm, vm->booleanFalse);
            break;

        case OP_INTEGER:
            push(vm, HeapBoxInteger(vm, BytecodeReadInt(&ip)));
            break;

        case OP_STRING:
            value = HeapCreatePooledString(vm, BytecodeReadRef(&ip));
            if (!value)
            {
                return;
            }
            push(vm, value);
            break;

        case OP_EMPTY_LIST:
            push(vm, vm->emptyList);
            break;

        case OP_LIST:
            size1 = BytecodeReadUint(&ip);
            assert(size1);
            objectData = HeapAlloc(vm, TYPE_ARRAY, size1 * sizeof(objectref));
            objectData += size1 * sizeof(objectref);
            while (size1--)
            {
                objectData -= sizeof(objectref);
                *(objectref*)objectData = pop(vm);
            }
            push(vm, HeapFinishAlloc(vm, objectData));
            break;

        case OP_FILE:
            string = BytecodeReadRef(&ip);
            file = FileAdd(StringPoolGetString(string),
                           StringPoolGetStringLength(string));
            value = HeapCreateFile(vm, file);
            if (!value)
            {
                return;
            }
            push(vm, value);
            break;

        case OP_FILESET:
            value = HeapCreateFilesetGlob(
                vm, StringPoolGetString(BytecodeReadRef(&ip)));
            if (!value)
            {
                return;
            }
            push(vm, value);
            break;

        case OP_POP:
            pop(vm);
            break;

        case OP_DUP:
            push(vm, peek(vm));
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
            push(vm, getLocal(vm, bp, BytecodeReadUint16(&ip)));
            break;

        case OP_STORE:
            storeLocal(vm, bp, BytecodeReadUint16(&ip), pop(vm));
            break;

        case OP_LOAD_FIELD:
            push(vm, vm->fields[BytecodeReadUint(&ip)]);
            break;

        case OP_STORE_FIELD:
            vm->fields[BytecodeReadUint(&ip)] = pop(vm);
            break;

        case OP_CAST_BOOLEAN:
            pushBoolean(vm, InterpreterPopBoolean(vm));
            break;

        case OP_EQUALS:
            pushBoolean(vm, HeapEquals(vm, pop(vm), pop(vm)));
            break;

        case OP_NOT_EQUALS:
            pushBoolean(vm, !HeapEquals(vm, pop(vm), pop(vm)));
            break;

        case OP_LESS_EQUALS:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, HeapCompare(vm, value2, value) <= 0);
            break;

        case OP_GREATER_EQUALS:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, HeapCompare(vm, value2, value) >= 0);
            break;

        case OP_LESS:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, HeapCompare(vm, value2, value) < 0);
            break;

        case OP_GREATER:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, HeapCompare(vm, value2, value) > 0);
            break;

        case OP_NOT:
            value = pop(vm);
            assert(value == vm->booleanTrue ||
                   value == vm->booleanFalse);
            pushBoolean(vm, value == vm->booleanFalse);
            break;

        case OP_NEG:
            assert(HeapUnboxInteger(vm, peek(vm)) != INT_MIN);
            push(vm, HeapBoxInteger(vm, -HeapUnboxInteger(vm, pop(vm))));
            break;

        case OP_INV:
            push(vm, HeapBoxInteger(vm, ~HeapUnboxInteger(vm, pop(vm))));
            break;

        case OP_ADD:
            value = pop(vm);
            value2 = pop(vm);
            push(vm, HeapBoxInteger(vm,
                                    HeapUnboxInteger(vm, value2) +
                                    HeapUnboxInteger(vm, value)));
            break;

        case OP_SUB:
            value = pop(vm);
            value2 = pop(vm);
            push(vm, HeapBoxInteger(vm,
                                    HeapUnboxInteger(vm, value2) -
                                    HeapUnboxInteger(vm, value)));
            break;

        case OP_MUL:
            value = pop(vm);
            value2 = pop(vm);
            push(vm, HeapBoxInteger(vm,
                                    HeapUnboxInteger(vm, value2) *
                                    HeapUnboxInteger(vm, value)));
            break;

        case OP_DIV:
            value = pop(vm);
            value2 = pop(vm);
            assert((HeapUnboxInteger(vm, value2) /
                    HeapUnboxInteger(vm, value)) *
                   HeapUnboxInteger(vm, value) ==
                   HeapUnboxInteger(vm, value2)); /* TODO: fraction */
            push(vm, HeapBoxInteger(vm,
                                    HeapUnboxInteger(vm, value2) /
                                    HeapUnboxInteger(vm, value)));
            break;

        case OP_REM:
            value = pop(vm);
            value2 = pop(vm);
            push(vm, HeapBoxInteger(vm,
                                    HeapUnboxInteger(vm, value2) %
                                    HeapUnboxInteger(vm, value)));
            break;

        case OP_CONCAT_STRING:
            value = pop(vm);
            value2 = pop(vm);
            size1 = HeapStringLength(vm, value2);
            size2 = HeapStringLength(vm, value);
            if (!size1 && !size2)
            {
                push(vm, vm->emptyString);
                break;
            }
            objectData = HeapAlloc(vm, TYPE_STRING, size1 + size2);
            HeapWriteString(vm, value2, (char*)objectData);
            HeapWriteString(vm, value, (char*)objectData + size1);
            push(vm, HeapFinishAlloc(vm, objectData));
            break;

        case OP_CONCAT_LIST:
            value = pop(vm);
            value2 = pop(vm);
            value = HeapConcatList(vm, value2, value);
            push(vm, value);
            break;

        case OP_INDEXED_ACCESS:
            value = pop(vm);
            value2 = pop(vm);
            HeapCollectionGet(vm, value2, value, &value);
            push(vm, value);
            break;

        case OP_RANGE:
            value = pop(vm);
            value2 = pop(vm);
            value = HeapCreateRange(vm, value2, value);
            push(vm, value);
            break;

        case OP_ITER_INIT:
            value = HeapCreateIterator(vm, pop(vm));
            push(vm, value);
            break;

        case OP_ITER_NEXT:
            value = pop(vm);
            assert(HeapGetObjectType(vm, value) == TYPE_ITERATOR);
            assert(HeapGetObjectSize(vm, value) == sizeof(Iterator));
            piter = (Iterator*)HeapGetObjectData(vm, value);
            value = 0;
            pushBoolean(vm, HeapIteratorNext(piter, &value));
            push(vm, value);
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            ip += jumpOffset;
            break;

        case OP_BRANCH_TRUE:
            assert(peek(vm) == vm->booleanTrue ||
                   peek(vm) == vm->booleanFalse);
            jumpOffset = BytecodeReadInt(&ip);
            if (pop(vm) == vm->booleanTrue)
            {
                ip += jumpOffset;
            }
            break;

        case OP_BRANCH_FALSE:
            assert(peek(vm) == vm->booleanTrue ||
                   peek(vm) == vm->booleanFalse);
            jumpOffset = BytecodeReadInt(&ip);
            if (pop(vm) != vm->booleanTrue)
            {
                ip += jumpOffset;
            }
            break;

        case OP_RETURN:
            assert(IntVectorSize(&vm->callStack));
            popStackFrame(vm, &ip, &bp, *ip++);
            baseIP = vm->bytecode +
                FunctionIndexGetBytecodeOffset(
                    FunctionIndexGetFunctionFromBytecode(
                        (uint)(ip - vm->bytecode)));
            break;

        case OP_RETURN_VOID:
            if (!IntVectorSize(&vm->callStack))
            {
                assert(IntVectorSize(&vm->stack) ==
                       FunctionIndexGetLocalsCount(target));
                return;
            }
            popStackFrame(vm, &ip, &bp, 0);
            baseIP = vm->bytecode +
                FunctionIndexGetBytecodeOffset(
                    FunctionIndexGetFunctionFromBytecode(
                        (uint)(ip - vm->bytecode)));
            break;

        case OP_INVOKE:
            function = BytecodeReadRef(&ip);
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == FunctionIndexGetParameterCount(function)); /* TODO */
            returnValueCount = *ip++;
            pushStackFrame(vm, &ip, &bp, function, returnValueCount);
            baseIP = ip;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = refFromUint(*ip++);
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == NativeGetParameterCount(nativeFunction)); /* TODO */
            NativeInvoke(vm, nativeFunction, *ip++);
            if (vm->error)
            {
                return;
            }
            break;

        case OP_UPTODATE:
            assert(!vm->currentCache);
            HashInit(&hashState);
            value = pop(vm);
            HeapHash(vm, value, &hashState);
            HashFinal(&hashState, hash);
            CacheGet(hash, &vm->currentCache);
            if (CacheIsNewEntry(vm->currentCache))
            {
                HeapIteratorInit(vm, &iter, value, true);
                while (HeapIteratorNext(&iter, &value))
                {
                    if (HeapGetObjectType(vm, value) == TYPE_FILE)
                    {
                        vm->error = CacheAddDependency(vm->currentCache,
                                                       HeapGetFile(vm, value));
                        if (vm->error)
                        {
                            return;
                        }
                    }
                }
            }
            pushBoolean(vm, CacheUptodate(vm->currentCache));
            value = HeapCreateFile(vm, CacheGetFile(vm->currentCache));
            push(vm, value);
            break;

        case OP_UPTODATE_FINISH:
            assert(vm->currentCache);
            vm->error = CacheSetUptodate(vm->currentCache);
            if (vm->error)
            {
                return;
            }
            vm->currentCache = 0;
            break;
        }
    }
}

static void disposeVM(VM *vm)
{
    HeapDispose(vm);
    free(vm->fields);
    IntVectorDispose(&vm->callStack);
    IntVectorDispose(&vm->stack);
}

ErrorCode InterpreterExecute(const byte *restrict bytecode, functionref target)
{
    VM vm;
    uint fieldCount = FieldIndexGetCount();

    memset(&vm, 0, sizeof(vm));
    vm.bytecode = bytecode;
    vm.fields = (objectref*)malloc(fieldCount * sizeof(int));
    HeapInit(&vm);
    IntVectorInit(&vm.callStack);
    IntVectorInit(&vm.stack);

    execute(&vm, FunctionIndexGetFirstFunction());
    if (!vm.error)
    {
        execute(&vm, target);
    }

    disposeVM(&vm);
    return vm.error;
}
