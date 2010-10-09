#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "fieldindex.h"
#include "fileindex.h"
#include "functionindex.h"
#include "instruction.h"
#include "interpreter.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"

static const boolean TRACE = false;


static boolean setError(VM *vm, ErrorCode error)
{
    vm->error = error;
    return error ? true : false;
}


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

boolean InterpreterPush(VM *vm, objectref value)
{
    return !setError(vm, IntVectorAddRef(&vm->stack, value));
}

static boolean pushBoolean(VM *vm, boolean value)
{
    if (value)
    {
        return push(vm, vm->booleanTrue);
    }
    return push(vm, vm->booleanFalse);
}


static objectref getLocal(VM *vm, uint bp, uint16 local)
{
    return IntVectorGetRef(&vm->stack, bp + local);
}

static void storeLocal(VM *vm, uint bp, uint16 local, objectref value)
{
    IntVectorSetRef(&vm->stack, bp + local, value);
}


static objectref createIterator(VM *vm, objectref object)
{
    Iterator *iter = (Iterator *)HeapAlloc(vm, TYPE_ITERATOR, sizeof(Iterator));
    HeapCollectionIteratorInit(vm, iter, object, false);
    return HeapFinishAlloc(vm, (byte*)iter);
}

static boolean equals(VM *vm, objectref value1, objectref value2)
{
    Iterator iter1;
    Iterator iter2;
    size_t size1;
    size_t size2;
    boolean success;

    if (value1 == value2)
    {
        return true;
    }
    switch (HeapGetObjectType(vm, value1))
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ITERATOR:
        return false;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        size1 = HeapGetStringLength(vm, value1);
        size2 = HeapGetStringLength(vm, value2);
        return size1 == size2 &&
            !memcmp(HeapGetString(vm, value1),
                    HeapGetString(vm, value2), size1);

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        if (!HeapIsCollection(vm, value2) ||
            HeapCollectionSize(vm, value1) !=
            HeapCollectionSize(vm, value2))
        {
            return false;
        }
        HeapCollectionIteratorInit(vm, &iter1, value1, false);
        HeapCollectionIteratorInit(vm, &iter2, value2, false);
        while (HeapIteratorNext(&iter1, &value1))
        {
            success = HeapIteratorNext(&iter2, &value2);
            assert(success);
            if (!equals(vm, value1, value2))
            {
                return false;
            }
        }
        return true;
    }
    assert(false);
    return false;
}

static int compare(VM *vm, objectref value1, objectref value2)
{
    int i1 = HeapUnboxInteger(vm, value1);
    int i2 = HeapUnboxInteger(vm, value2);
    return i1 == i2 ? 0 : i1 < i2 ? -1 : 1;
}

bytevector *InterpreterGetPipeOut(VM *vm)
{
    return vm->pipeOut;
}

bytevector *InterpreterGetPipeErr(VM *vm)
{
    return vm->pipeErr;
}

const char *InterpreterGetString(VM *vm, objectref value)
{
    size_t size = InterpreterGetStringSize(vm, value);
    byte *buffer = (byte*)malloc(size + 1); /* TODO: Avoid malloc */
    assert(buffer); /* TODO: Error handling */
    InterpreterCopyString(vm, value, buffer);
    buffer[size] = 0;
    return (char*)buffer;
}

void InterpreterFreeStringBuffer(VM *vm unused, const char *buffer)
{
    free((void*)buffer);
}

size_t InterpreterGetStringSize(VM *vm, objectref value)
{
    Iterator iter;
    uint i;
    size_t size;

    if (!value)
    {
        return 4;
    }
    switch (HeapGetObjectType(vm, value))
    {
    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(vm, value);
        size = 1;
        if ((int)i < 0)
        {
            size = 2;
            i = -i;
        }
        while (i > 9)
        {
            i /= 10;
            size++;
        }
        return size;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        return HeapGetStringLength(vm, value);

    case TYPE_FILE:
        return strlen(FileIndexGetName(HeapGetFile(vm, value)));

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        size = HeapCollectionSize(vm, value);
        if (size)
        {
            size--;
        }
        size = size * 2 + 2;
        HeapCollectionIteratorInit(vm, &iter, value, false);
        while (HeapIteratorNext(&iter, &value))
        {
            size += InterpreterGetStringSize(vm, value);
        }
        return size;

    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return 0;
}

byte *InterpreterCopyString(VM *vm, objectref value, byte *dst)
{
    Iterator iter;
    size_t size;
    fileref file;
    uint i;
    boolean first;

    if (!value)
    {
        *dst++ = 'n';
        *dst++ = 'u';
        *dst++ = 'l';
        *dst++ = 'l';
        return dst;
    }
    switch (HeapGetObjectType(vm, value))
    {
    case TYPE_BOOLEAN_TRUE:
        *dst++ = 't';
        *dst++ = 'r';
        *dst++ = 'u';
        *dst++ = 'e';
        return dst;

    case TYPE_BOOLEAN_FALSE:
        *dst++ = 'f';
        *dst++ = 'a';
        *dst++ = 'l';
        *dst++ = 's';
        *dst++ = 'e';
        return dst;

    case TYPE_INTEGER:
        i = (uint)HeapUnboxInteger(vm, value);
        if (!i)
        {
            *dst++ = '0';
            return dst;
        }
        size = InterpreterGetStringSize(vm, value);
        if ((int)i < 0)
        {
            *dst++ = '-';
            size--;
            i = -i;
        }
        dst += size - 1;
        while (i)
        {
            *dst-- = (byte)('0' + i % 10);
            i /= 10;
        }
        return dst + size + 1;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        size = HeapGetStringLength(vm, value);
        memcpy(dst, HeapGetString(vm, value), size);
        return dst + size;

    case TYPE_FILE:
        file = HeapGetFile(vm, value);
        size = strlen(FileIndexGetName(file));
        memcpy(dst, FileIndexGetName(file), size);
        return dst + size;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        *dst++ = '[';
        first = true;
        HeapCollectionIteratorInit(vm, &iter, value, false);
        while (HeapIteratorNext(&iter, &value))
        {
            if (!first)
            {
                *dst++ = ',';
                *dst++ = ' ';
            }
            first = false;
            dst = InterpreterCopyString(vm, value, dst);
        }
        *dst++ = ']';
        return dst;

    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return null;
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
    Iterator *iter;
    functionref function;
    nativefunctionref nativeFunction;
    fileref file;

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
            objectData = HeapAlloc(vm, TYPE_ARRAY,
                                   size1 * sizeof(objectref));
            if (!objectData)
            {
                vm->error = OUT_OF_MEMORY;
                return;
            }
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
            file = FileIndexAdd(StringPoolGetString(string),
                                 StringPoolGetStringLength(string));
            if (!file)
            {
                vm->error = OUT_OF_MEMORY;
                return;
            }
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
            value = pop(vm);
            if (value == vm->booleanTrue ||
                value == vm->booleanFalse)
            {
                push(vm, value);
            }
            else if (!value)
            {
                push(vm, vm->booleanFalse);
            }
            else if (HeapGetObjectType(vm, value) == TYPE_INTEGER)
            {
                pushBoolean(vm, HeapUnboxInteger(vm, value) != 0);
            }
            else if (HeapIsString(vm, value))
            {
                pushBoolean(vm, HeapGetStringLength(vm, value) != 0);
            }
            else if (HeapIsCollection(vm, value))
            {
                pushBoolean(vm, HeapCollectionSize(vm, value) != 0);
            }
            else
            {
                push(vm, vm->booleanTrue);
            }
            break;

        case OP_EQUALS:
            pushBoolean(vm, equals(vm, pop(vm), pop(vm)));
            break;

        case OP_NOT_EQUALS:
            pushBoolean(vm, !equals(vm, pop(vm), pop(vm)));
            break;

        case OP_LESS_EQUALS:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, compare(vm, value2, value) <= 0);
            break;

        case OP_GREATER_EQUALS:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, compare(vm, value2, value) >= 0);
            break;

        case OP_LESS:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, compare(vm, value2, value) < 0);
            break;

        case OP_GREATER:
            value = pop(vm);
            value2 = pop(vm);
            pushBoolean(vm, compare(vm, value2, value) > 0);
            break;

        case OP_NOT:
            value = pop(vm);
            assert(value == vm->booleanTrue ||
                   value == vm->booleanFalse);
            pushBoolean(vm, value == vm->booleanFalse);
            break;

        case OP_NEG:
            assert(HeapUnboxInteger(vm, peek(vm)) != INT_MIN);
            push(vm,
                 HeapBoxInteger(vm,
                                -HeapUnboxInteger(vm, pop(vm))));
            break;

        case OP_INV:
            push(vm,
                 HeapBoxInteger(vm,
                                ~HeapUnboxInteger(vm, pop(vm))));
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

        case OP_CONCAT:
            value = pop(vm);
            value2 = pop(vm);
            size1 = InterpreterGetStringSize(vm, value2);
            size2 = InterpreterGetStringSize(vm, value);
            if (!size1 && !size2)
            {
                push(vm, vm->emptyString);
                break;
            }
            objectData = HeapAlloc(vm, TYPE_STRING, size1 + size2);
            if (!objectData)
            {
                vm->error = OUT_OF_MEMORY;
                return;
            }
            InterpreterCopyString(vm, value2, objectData);
            InterpreterCopyString(vm, value, objectData + size1);
            push(vm, HeapFinishAlloc(vm, objectData));
            break;

        case OP_INDEXED_ACCESS:
            value = pop(vm);
            value2 = pop(vm);
            if (!HeapCollectionGet(vm, value2, value, &value))
            {
                return;
            }
            push(vm, value);
            break;

        case OP_RANGE:
            value = pop(vm);
            value2 = pop(vm);
            value = HeapCreateRange(vm, value2, value);
            if (!value)
            {
                return;
            }
            push(vm, value);
            break;

        case OP_ITER_INIT:
            value = createIterator(vm, pop(vm));
            if (!value)
            {
                return;
            }
            push(vm, value);
            break;

        case OP_ITER_NEXT:
            value = pop(vm);
            assert(HeapGetObjectType(vm, value) == TYPE_ITERATOR);
            assert(HeapGetObjectSize(vm, value) == sizeof(Iterator));
            iter = (Iterator*)HeapGetObjectData(vm, value);
            value = 0;
            pushBoolean(vm, HeapIteratorNext(iter, &value));
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
            vm->error = NativeInvoke(vm, nativeFunction, *ip++);
            if (vm->error)
            {
                return;
            }
            break;

        case OP_PIPE_BEGIN:
            assert(!vm->pipeOut);
            assert(!vm->pipeErr);
            vm->pipeOut = ByteVectorCreate();
            vm->pipeErr = ByteVectorCreate();
            if (!vm->pipeOut || !vm->pipeErr)
            {
                vm->error = OUT_OF_MEMORY;
                return;
            }
            break;

        case OP_PIPE_END:
            assert(vm->pipeOut);
            value = HeapCreateString(
                vm,
                (const char*)ByteVectorGetPointer(vm->pipeOut, 0),
                ByteVectorSize(vm->pipeOut));
            if (!value)
            {
                vm->error = OUT_OF_MEMORY;
                return;
            }
            storeLocal(vm, bp, BytecodeReadUint16(&ip), value);
            ByteVectorDispose(vm->pipeOut);
            free(vm->pipeOut);
            vm->pipeOut = null;

            assert(vm->pipeErr);
            value = HeapCreateString(
                vm,
                (const char*)ByteVectorGetPointer(vm->pipeErr, 0),
                ByteVectorSize(vm->pipeErr));
            if (!value)
            {
                vm->error = OUT_OF_MEMORY;
                return;
            }
            storeLocal(vm, bp, BytecodeReadUint16(&ip), value);
            ByteVectorDispose(vm->pipeErr);
            free(vm->pipeErr);
            vm->pipeErr = null;

            if (vm->error)
            {
                return;
            }
            break;
        }
    }
}

static void disposeVm(VM *vm)
{
    HeapDispose(vm);
    free(vm->fields);
    IntVectorDispose(&vm->callStack);
    IntVectorDispose(&vm->stack);
}

static boolean handleError(VM *vm, ErrorCode error)
{
    vm->error = error;
    if (error)
    {
        disposeVm(vm);
        return true;
    }
    return false;
}

ErrorCode InterpreterExecute(const byte *restrict bytecode, functionref target)
{
    VM vm;
    uint fieldCount = FieldIndexGetCount();

    memset(&vm, 0, sizeof(vm));
    vm.bytecode = bytecode;
    vm.fields = (objectref*)malloc(fieldCount * sizeof(int));
    if (handleError(&vm, vm.fields ? NO_ERROR : OUT_OF_MEMORY) ||
        handleError(&vm, HeapInit(&vm)) ||
        handleError(&vm, IntVectorInit(&vm.callStack)) ||
        handleError(&vm, IntVectorInit(&vm.stack)))
    {
        return vm.error;
    }

    execute(&vm, FunctionIndexGetFirstFunction());
    if (!vm.error)
    {
        execute(&vm, target);
    }

    disposeVm(&vm);

    return vm.error;
}
