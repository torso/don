#include <stdio.h>
#include <memory.h>
#include "common.h"
#include "bytecode.h"
#include "bytevector.h"
#include "fieldindex.h"
#include "fileindex.h"
#include "functionindex.h"
#include "heap.h"
#include "instruction.h"
#include "interpreter.h"
#include "intvector.h"
#include "math.h"
#include "native.h"
#include "stringpool.h"

static const boolean TRACE = false;


struct RunState
{
    const byte *restrict bytecode;

    Heap heap;
    uint *fields;
    intvector callStack;
    intvector stack;
    ErrorCode error;

    bytevector *pipeOut;
    bytevector *pipeErr;
};


static boolean setError(RunState *state, ErrorCode error)
{
    state->error = error;
    return error ? true : false;
}


#define peek InterpreterPeek
#define pop InterpreterPop
#define push InterpreterPush

uint InterpreterPeek(RunState *state)
{
    return IntVectorPeek(&state->stack);
}

uint InterpreterPop(RunState *state)
{
    return IntVectorPop(&state->stack);
}

boolean InterpreterPush(RunState *state, uint value)
{
    return !setError(state, IntVectorAdd(&state->stack, value));
}

static boolean pushBoolean(RunState *state, boolean value)
{
    return push(state,
                value ? state->heap.booleanTrue : state->heap.booleanFalse);
}


static void storeLocal(RunState *state, uint bp, uint16 local, uint value)
{
    IntVectorSet(&state->stack, bp + local, value);
}

static uint createIterator(RunState *state, uint object)
{
    Iterator *iter = (Iterator *)HeapAlloc(&state->heap, TYPE_ITERATOR,
                                           sizeof(Iterator));
    HeapCollectionIteratorInit(&state->heap, iter, object, false);
    return HeapFinishAlloc(&state->heap, (byte*)iter);
}

static boolean equals(RunState *state, uint value1, uint value2)
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
    switch (HeapGetObjectType(&state->heap, value1))
    {
    case TYPE_BOOLEAN_TRUE:
    case TYPE_BOOLEAN_FALSE:
    case TYPE_INTEGER:
    case TYPE_FILE:
    case TYPE_ITERATOR:
        return false;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        size1 = HeapGetStringLength(&state->heap, value1);
        size2 = HeapGetStringLength(&state->heap, value2);
        return size1 == size2 &&
            !memcmp(HeapGetString(&state->heap, value1),
                    HeapGetString(&state->heap, value2), size1);

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        if (!HeapIsCollection(&state->heap, value2) ||
            HeapCollectionSize(&state->heap, value1) !=
            HeapCollectionSize(&state->heap, value2))
        {
            return false;
        }
        HeapCollectionIteratorInit(&state->heap, &iter1, value1, false);
        HeapCollectionIteratorInit(&state->heap, &iter2, value2, false);
        while (HeapIteratorNext(&iter1, &value1))
        {
            success = HeapIteratorNext(&iter2, &value2);
            assert(success);
            if (!equals(state, value1, value2))
            {
                return false;
            }
        }
        return true;
    }
    assert(false);
    return false;
}

static int compare(RunState *state, uint value1, uint value2)
{
    int i1 = HeapUnboxInteger(&state->heap, value1);
    int i2 = HeapUnboxInteger(&state->heap, value2);
    return i1 == i2 ? 0 : i1 < i2 ? -1 : 1;
}

Heap *InterpreterGetHeap(RunState *state)
{
    return &state->heap;
}

bytevector *InterpreterGetPipeOut(RunState *state)
{
    return state->pipeOut;
}

bytevector *InterpreterGetPipeErr(RunState *state)
{
    return state->pipeErr;
}

const char *InterpreterGetString(RunState *state, uint value)
{
    size_t size = InterpreterGetStringSize(state, value);
    byte *buffer = (byte*)malloc(size + 1); /* TODO: Avoid malloc */
    assert(buffer); /* TODO: Error handling */
    InterpreterCopyString(state, value, buffer);
    buffer[size] = 0;
    return (char*)buffer;
}

void InterpreterFreeStringBuffer(RunState *state unused, const char *buffer)
{
    free((void*)buffer);
}

size_t InterpreterGetStringSize(RunState *state, uint value)
{
    Iterator iter;
    size_t size;

    if (!value)
    {
        return 4;
    }
    switch (HeapGetObjectType(&state->heap, value))
    {
    case TYPE_BOOLEAN_TRUE:
        return 4;

    case TYPE_BOOLEAN_FALSE:
        return 5;

    case TYPE_INTEGER:
        value = (uint)HeapUnboxInteger(&state->heap, value);
        size = 1;
        if ((int)value < 0)
        {
            size = 2;
            value = -value;
        }
        while (value > 9)
        {
            value /= 10;
            size++;
        }
        return size;

    case TYPE_STRING:
    case TYPE_STRING_POOLED:
        return HeapGetStringLength(&state->heap, value);

    case TYPE_FILE:
        return strlen(FileIndexGetName(HeapGetFile(&state->heap, value)));

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        size = HeapCollectionSize(&state->heap, value);
        if (size)
        {
            size--;
        }
        size = size * 2 + 2;
        HeapCollectionIteratorInit(&state->heap, &iter, value, false);
        while (HeapIteratorNext(&iter, &value))
        {
            size += InterpreterGetStringSize(state, value);
        }
        return size;

    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return 0;
}

byte *InterpreterCopyString(RunState *state, uint value, byte *dst)
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
    switch (HeapGetObjectType(&state->heap, value))
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
        i = (uint)HeapUnboxInteger(&state->heap, value);
        if (!i)
        {
            *dst++ = '0';
            return dst;
        }
        size = InterpreterGetStringSize(state, value);
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
        size = HeapGetStringLength(&state->heap, value);
        memcpy(dst, HeapGetString(&state->heap, value), size);
        return dst + size;

    case TYPE_FILE:
        file = HeapGetFile(&state->heap, value);
        size = strlen(FileIndexGetName(file));
        memcpy(dst, FileIndexGetName(file), size);
        return dst + size;

    case TYPE_EMPTY_LIST:
    case TYPE_ARRAY:
    case TYPE_INTEGER_RANGE:
        *dst++ = '[';
        first = true;
        HeapCollectionIteratorInit(&state->heap, &iter, value, false);
        while (HeapIteratorNext(&iter, &value))
        {
            if (!first)
            {
                *dst++ = ',';
                *dst++ = ' ';
            }
            first = false;
            dst = InterpreterCopyString(state, value, dst);
        }
        *dst++ = ']';
        return dst;

    case TYPE_ITERATOR:
        break;
    }
    assert(false);
    return null;
}

static void pushStackFrame(RunState *state, const byte **ip, uint *bp,
                           functionref function, uint returnValues)
{
    uint localsCount;
    IntVectorAdd(&state->callStack, (uint)(*ip - state->bytecode));
    IntVectorAdd(&state->callStack, *bp);
    IntVectorAdd(&state->callStack, returnValues);
    *ip = state->bytecode + FunctionIndexGetBytecodeOffset(function);
    *bp = (uint)IntVectorSize(&state->stack) -
        FunctionIndexGetParameterCount(function);
    localsCount = FunctionIndexGetLocalsCount(function);
    IntVectorSetSize(&state->stack, *bp + localsCount);
}

static void popStackFrame(RunState *state, const byte **ip, uint *bp,
                          uint returnValues)
{
    uint expectedReturnValues = IntVectorPop(&state->callStack);

    IntVectorCopy(&state->stack,
                  IntVectorSize(&state->stack) - returnValues,
                  &state->stack,
                  *bp,
                  expectedReturnValues);
    IntVectorSetSize(&state->stack, *bp + expectedReturnValues);

    *bp = IntVectorPop(&state->callStack);
    *ip = state->bytecode + IntVectorPop(&state->callStack);
}

static void execute(RunState *state, functionref target)
{
    const byte *ip = state->bytecode + FunctionIndexGetBytecodeOffset(target);
    const byte *baseIP = ip;
    uint bp = 0;
    uint argumentCount;
    int jumpOffset;
    uint local;
    uint value;
    uint value2;
    size_t size1;
    size_t size2;
    stringref string;
    byte *objectData;
    Iterator *iter;
    functionref function;
    nativefunctionref nativeFunction;
    fileref file;

    local = FunctionIndexGetLocalsCount(target);
    IntVectorSetSize(&state->stack, local);
    for (;;)
    {
        if (TRACE)
        {
            BytecodeDisassembleInstruction(ip, baseIP);
        }
        switch ((Instruction)*ip++)
        {
        case OP_NULL:
            push(state, 0);
            break;

        case OP_TRUE:
            push(state, state->heap.booleanTrue);
            break;

        case OP_FALSE:
            push(state, state->heap.booleanFalse);
            break;

        case OP_INTEGER:
            push(state, HeapBoxInteger(&state->heap, BytecodeReadInt(&ip)));
            break;

        case OP_STRING:
            value = HeapCreatePooledString(&state->heap, (stringref)BytecodeReadUint(&ip));
            if (!value)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            push(state, value);
            break;

        case OP_EMPTY_LIST:
            push(state, state->heap.emptyList);
            break;

        case OP_LIST:
            size1 = BytecodeReadUint(&ip);
            objectData = HeapAlloc(&state->heap, TYPE_ARRAY,
                                   size1 * sizeof(uint));
            if (!objectData)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            objectData += size1 * sizeof(uint);
            while (size1--)
            {
                objectData -= sizeof(uint);
                *(uint*)objectData = pop(state);
            }
            push(state, HeapFinishAlloc(&state->heap, objectData));
            break;

        case OP_FILE:
            string = (stringref)BytecodeReadUint(&ip);
            file = FileIndexAdd(StringPoolGetString(string),
                                 StringPoolGetStringLength(string));
            if (!file)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            value = HeapCreateFile(&state->heap, file);
            if (!value)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            push(state, value);
            break;

        case OP_FILESET:
            state->error = HeapCreateFilesetGlob(
                &state->heap,
                StringPoolGetString((stringref)BytecodeReadUint(&ip)),
                &value);
            if (state->error)
            {
                return;
            }
            push(state, value);
            break;

        case OP_POP:
            pop(state);
            break;

        case OP_DUP:
            push(state, peek(state));
            break;

        case OP_LOAD:
            local = BytecodeReadUint16(&ip);
            push(state, IntVectorGet(&state->stack, bp + local));
            break;

        case OP_STORE:
            storeLocal(state, bp, BytecodeReadUint16(&ip), pop(state));
            break;

        case OP_LOAD_FIELD:
            value = BytecodeReadUint(&ip);
            push(state, state->fields[value]);
            break;

        case OP_STORE_FIELD:
            state->fields[BytecodeReadUint(&ip)] = pop(state);
            break;

        case OP_CAST_BOOLEAN:
            value = pop(state);
            if (value == state->heap.booleanTrue ||
                value == state->heap.booleanFalse)
            {
                push(state, value);
            }
            else if (!value)
            {
                push(state, state->heap.booleanFalse);
            }
            else if (HeapGetObjectType(&state->heap, value) == TYPE_INTEGER)
            {
                pushBoolean(state, HeapUnboxInteger(&state->heap, value) != 0);
            }
            break;

        case OP_EQUALS:
            pushBoolean(state, equals(state, pop(state), pop(state)));
            break;

        case OP_NOT_EQUALS:
            pushBoolean(state, !equals(state, pop(state), pop(state)));
            break;

        case OP_LESS_EQUALS:
            value = pop(state);
            value2 = pop(state);
            pushBoolean(state, compare(state, value2, value) <= 0);
            break;

        case OP_GREATER_EQUALS:
            value = pop(state);
            value2 = pop(state);
            pushBoolean(state, compare(state, value2, value) >= 0);
            break;

        case OP_LESS:
            value = pop(state);
            value2 = pop(state);
            pushBoolean(state, compare(state, value2, value) < 0);
            break;

        case OP_GREATER:
            value = pop(state);
            value2 = pop(state);
            pushBoolean(state, compare(state, value2, value) > 0);
            break;

        case OP_NOT:
            value = pop(state);
            assert(value == state->heap.booleanTrue ||
                   value == state->heap.booleanFalse);
            pushBoolean(state, value == state->heap.booleanFalse);
            break;

        case OP_NEG:
            assert(HeapUnboxInteger(&state->heap, peek(state)) != MIN_INT);
            push(state,
                 HeapBoxInteger(&state->heap,
                                -HeapUnboxInteger(&state->heap, pop(state))));
            break;

        case OP_INV:
            push(state,
                 HeapBoxInteger(&state->heap,
                                ~HeapUnboxInteger(&state->heap, pop(state))));
            break;

        case OP_ADD:
            value = pop(state);
            value2 = pop(state);
            push(state, HeapBoxInteger(&state->heap,
                                       HeapUnboxInteger(&state->heap, value2) +
                                       HeapUnboxInteger(&state->heap, value)));
            break;

        case OP_SUB:
            value = pop(state);
            value2 = pop(state);
            push(state, HeapBoxInteger(&state->heap,
                                       HeapUnboxInteger(&state->heap, value2) -
                                       HeapUnboxInteger(&state->heap, value)));
            break;

        case OP_MUL:
            value = pop(state);
            value2 = pop(state);
            push(state, HeapBoxInteger(&state->heap,
                                       HeapUnboxInteger(&state->heap, value2) *
                                       HeapUnboxInteger(&state->heap, value)));
            break;

        case OP_DIV:
            value = pop(state);
            value2 = pop(state);
            assert((HeapUnboxInteger(&state->heap, value2) /
                    HeapUnboxInteger(&state->heap, value)) *
                   HeapUnboxInteger(&state->heap, value) ==
                   HeapUnboxInteger(&state->heap, value2)); /* TODO: fraction */
            push(state, HeapBoxInteger(&state->heap,
                                       HeapUnboxInteger(&state->heap, value2) /
                                       HeapUnboxInteger(&state->heap, value)));
            break;

        case OP_REM:
            value = pop(state);
            value2 = pop(state);
            push(state, HeapBoxInteger(&state->heap,
                                       HeapUnboxInteger(&state->heap, value2) %
                                       HeapUnboxInteger(&state->heap, value)));
            break;

        case OP_CONCAT:
            value = pop(state);
            value2 = pop(state);
            size1 = InterpreterGetStringSize(state, value2);
            size2 = InterpreterGetStringSize(state, value);
            if (!size1 && !size2)
            {
                push(state, state->heap.emptyString);
                break;
            }
            objectData = HeapAlloc(&state->heap, TYPE_STRING, size1 + size2);
            if (!objectData)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            InterpreterCopyString(state, value2, objectData);
            InterpreterCopyString(state, value, objectData + size1);
            push(state, HeapFinishAlloc(&state->heap, objectData));
            break;

        case OP_INDEXED_ACCESS:
            value = pop(state);
            value2 = pop(state);
            if (!HeapCollectionGet(&state->heap, value2, value, &value))
            {
                return;
            }
            push(state, value);
            break;

        case OP_RANGE:
            value = pop(state);
            value2 = pop(state);
            value = HeapCreateRange(&state->heap, value2, value);
            if (!value)
            {
                return;
            }
            push(state, value);
            break;

        case OP_ITER_INIT:
            value = createIterator(state, pop(state));
            if (!value)
            {
                return;
            }
            push(state, value);
            break;

        case OP_ITER_NEXT:
            value = pop(state);
            assert(HeapGetObjectType(&state->heap, value) == TYPE_ITERATOR);
            assert(HeapGetObjectSize(&state->heap, value) == sizeof(Iterator));
            iter = (Iterator*)HeapGetObjectData(&state->heap, value);
            value = 0;
            pushBoolean(state, HeapIteratorNext(iter, &value));
            push(state, value);
            break;

        case OP_JUMP:
            jumpOffset = BytecodeReadInt(&ip);
            ip += jumpOffset;
            break;

        case OP_BRANCH_TRUE:
            assert(peek(state) == state->heap.booleanTrue ||
                   peek(state) == state->heap.booleanFalse);
            jumpOffset = BytecodeReadInt(&ip);
            if (pop(state) == state->heap.booleanTrue)
            {
                ip += jumpOffset;
            }
            break;

        case OP_BRANCH_FALSE:
            assert(peek(state) == state->heap.booleanTrue ||
                   peek(state) == state->heap.booleanFalse);
            jumpOffset = BytecodeReadInt(&ip);
            if (pop(state) != state->heap.booleanTrue)
            {
                ip += jumpOffset;
            }
            break;

        case OP_RETURN:
            assert(IntVectorSize(&state->callStack));
            popStackFrame(state, &ip, &bp, *ip++);
            baseIP = state->bytecode +
                FunctionIndexGetBytecodeOffset(
                    FunctionIndexGetFunctionFromBytecode(
                        (uint)(ip - state->bytecode)));
            break;

        case OP_RETURN_VOID:
            if (!IntVectorSize(&state->callStack))
            {
                assert(IntVectorSize(&state->stack) ==
                       FunctionIndexGetLocalsCount(target));
                return;
            }
            popStackFrame(state, &ip, &bp, 0);
            baseIP = state->bytecode +
                FunctionIndexGetBytecodeOffset(
                    FunctionIndexGetFunctionFromBytecode(
                        (uint)(ip - state->bytecode)));
            break;

        case OP_INVOKE:
            function = (functionref)BytecodeReadUint(&ip);
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == FunctionIndexGetParameterCount(function)); /* TODO */
            value = *ip++;
            pushStackFrame(state, &ip, &bp, function, value);
            baseIP = ip;
            break;

        case OP_INVOKE_NATIVE:
            nativeFunction = (nativefunctionref)*ip++;
            argumentCount = BytecodeReadUint16(&ip);
            assert(argumentCount == NativeGetParameterCount(nativeFunction)); /* TODO */
            state->error = NativeInvoke(state, nativeFunction, *ip++);
            if (state->error)
            {
                return;
            }
            break;

        case OP_PIPE_BEGIN:
            assert(!state->pipeOut);
            assert(!state->pipeErr);
            state->pipeOut = ByteVectorCreate();
            state->pipeErr = ByteVectorCreate();
            if (!state->pipeOut || !state->pipeErr)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            break;

        case OP_PIPE_END:
            assert(state->pipeOut);
            value = HeapCreateString(
                &state->heap,
                (const char*)ByteVectorGetPointer(state->pipeOut, 0),
                ByteVectorSize(state->pipeOut));
            if (!value)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            storeLocal(state, bp, BytecodeReadUint16(&ip), value);
            ByteVectorDispose(state->pipeOut);
            free(state->pipeOut);
            state->pipeOut = null;

            assert(state->pipeErr);
            value = HeapCreateString(
                &state->heap,
                (const char*)ByteVectorGetPointer(state->pipeErr, 0),
                ByteVectorSize(state->pipeErr));
            if (!value)
            {
                state->error = OUT_OF_MEMORY;
                return;
            }
            storeLocal(state, bp, BytecodeReadUint16(&ip), value);
            ByteVectorDispose(state->pipeErr);
            free(state->pipeErr);
            state->pipeErr = null;

            if (state->error)
            {
                return;
            }
            break;
        }
    }
}

static void disposeState(RunState *state)
{
    HeapDispose(&state->heap);
    free(state->fields);
    IntVectorDispose(&state->callStack);
    IntVectorDispose(&state->stack);
}

static boolean handleError(RunState *state, ErrorCode error)
{
    state->error = error;
    if (error)
    {
        disposeState(state);
        return true;
    }
    return false;
}

ErrorCode InterpreterExecute(const byte *restrict bytecode, functionref target)
{
    RunState state;
    uint fieldCount = FieldIndexGetCount();

    memset(&state, 0, sizeof(state));
    state.bytecode = bytecode;
    state.fields = (uint*)malloc(fieldCount * sizeof(int));
    if (handleError(&state, state.fields ? NO_ERROR : OUT_OF_MEMORY) ||
        handleError(&state, HeapInit(&state.heap)) ||
        handleError(&state, IntVectorInit(&state.callStack)) ||
        handleError(&state, IntVectorInit(&state.stack)))
    {
        return state.error;
    }

    execute(&state, FunctionIndexGetFirstFunction());
    if (!state.error)
    {
        execute(&state, target);
    }

    disposeState(&state);

    return state.error;
}
