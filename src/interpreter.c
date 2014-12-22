#include "common.h"
#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "heap.h"
#include "interpreter.h"
#include "instruction.h"
#include "linker.h"
#include "native.h"
#include "work.h"
#include "vm.h"

#ifdef DEBUG
bool trace;
static const char *lastFilename;
static int lastLine;

static void traceLine(int bytecodeOffset)
{
    const char *filename;
    int line;

    line = BytecodeLineNumber(vmLineNumbers, bytecodeOffset, &filename);
    if (filename != lastFilename || line != lastLine)
    {
        printf("%s:%d\n", filename, line);
        lastFilename = filename;
        lastLine = line;
    }
}
#endif

static intvector temp;
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
        return refFromInt(IVGet(&vm->stack, (size_t)(bp + variable)));
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
        IVSet(&vm->stack, (size_t)(bp + variable), intFromRef(value));
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
#ifdef DEBUG
    if (trace)
    {
        traceLine(functionOffset);
        printf("[%p] %u: ", (void*)vm, functionOffset);
        BytecodeDisassembleInstruction(vmBytecode + functionOffset, vmBytecode);
    }
#endif
    assert((i & 0xff) == OP_FUNCTION);
    *ip = bytecode;
    *bp = (int)(IVSize(&vm->stack) - parameterCount);
    IVGrowZero(&vm->stack, (size_t)localsCount);
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
    vref value;
    vref string;
    int function;
    nativefunctionref nativeFunction;

    for (;;)
    {
        int i = *vm->ip;
        int arg = i >> 8;
#ifdef DEBUG
        if (trace)
        {
            traceLine((int)(vm->ip - vmBytecode));
            printf("[%p] %u: ", (void*)vm, (uint)(vm->ip - vmBytecode));
            BytecodeDisassembleInstruction(vm->ip, vmBytecode);
            fflush(stdout);
        }
#endif
        vm->ip++;
        switch ((Instruction)(i & 0xff))
        {
        case OP_NULL:
            storeValue(vm, vm->bp, arg, HeapNull);
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
            array = VCreateArray((size_t)arg);
            for (write = array; arg--; write++)
            {
                *write = loadValue(vm, vm->bp, *vm->ip++);
            }
            storeValue(vm, vm->bp, *vm->ip++, VFinishArray(array));
            break;
        }

        case OP_FILELIST:
            string = refFromInt(arg);
            storeValue(vm, vm->bp, *vm->ip++,
                       HeapCreateFilelistGlob(HeapGetString(string), VStringLength(string)));
            break;

        case OP_STORE_CONSTANT:
            storeValue(vm, vm->bp, arg, refFromInt(*vm->ip++));
            break;

        case OP_COPY:
            storeValue(vm, vm->bp, *vm->ip++, loadValue(vm, vm->bp, arg));
            break;

        case OP_NOT:
            storeValue(vm, vm->bp, *vm->ip++,
                       VNot(vm, loadValue(vm, vm->bp, arg)));
            break;

        case OP_NEG:
            storeValue(vm, vm->bp, *vm->ip++,
                       VNeg(vm, loadValue(vm, vm->bp, arg)));
            break;

        case OP_INV:
            storeValue(vm, vm->bp, *vm->ip++,
                       VInv(vm, loadValue(vm, vm->bp, arg)));
            break;

        case OP_ITER_NEXT:
        {
            vref collection = loadValue(vm, vm->bp, *vm->ip++);
            int indexVariable = *vm->ip++;
            vref index = VAdd(vm, loadValue(vm, vm->bp, indexVariable),
                              loadValue(vm, vm->bp, *vm->ip++));
            storeValue(vm, vm->bp, indexVariable, index);
            value = VValidIndex(vm, collection, index);
            switch (VGetBool(value))
            {
            case TRUTHY:
                storeValue(vm, vm->bp, *vm->ip++, VIndexedAccess(vm, collection, index));
                break;
            case FALSY:
                vm->ip += arg - 2;
                break;
            case FUTURE:
            {
                VM *clone = VMClone(vm, value, vm->ip);
                addVM(clone);
                vm->ip += arg - 2;
                storeValue(clone, clone->bp, *clone->ip++,
                           VIndexedAccess(clone, collection, index));
                break;
            }
            }
            return;
        }

        case OP_EQUALS:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VEquals(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_NOT_EQUALS:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VNotEquals(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_LESS_EQUALS:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VLessEquals(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_GREATER_EQUALS:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VNot(vm, VLess(vm, loadValue(vm, vm->bp, arg), value));
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_LESS:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VLess(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_GREATER:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value =
                VNot(vm, VLessEquals(vm, loadValue(vm, vm->bp, arg), value));
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_AND:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VAnd(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_ADD:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VAdd(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_SUB:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VSub(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_MUL:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VMul(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_DIV:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VDiv(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_REM:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VRem(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_CONCAT_LIST:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VConcat(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_CONCAT_STRING:
            assert(!IVSize(&temp));
            for (i = 0; i < arg; i++)
            {
                IVAdd(&temp, intFromRef(loadValue(vm, vm->bp, *vm->ip++)));
            }
            value = VConcatString(vm, (size_t)arg, (vref*)IVGetWritePointer(&temp, 0));
            storeValue(vm, vm->bp, *vm->ip++, value);
            IVSetSize(&temp, 0);
            break;

        case OP_INDEXED_ACCESS:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VIndexedAccess(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_RANGE:
            value = loadValue(vm, vm->bp, *vm->ip++);
            value = VRange(vm, loadValue(vm, vm->bp, arg), value);
            storeValue(vm, vm->bp, *vm->ip++, value);
            break;

        case OP_JUMP:
            vm->ip += arg + 1;
            return;

        case OP_BRANCH_TRUE:
            value = loadValue(vm, vm->bp, *vm->ip++);
            switch (VGetBool(value))
            {
            case TRUTHY:
                vm->ip += arg;
                break;
            case FALSY:
                break;
            case FUTURE:
                addVM(VMClone(vm, value, vm->ip + arg));
                break;
            }
            return;

        case OP_BRANCH_FALSE:
            value = loadValue(vm, vm->bp, *vm->ip++);
            switch (VGetBool(value))
            {
            case TRUTHY:
                break;
            case FALSY:
                vm->ip += arg;
                break;
            case FUTURE:
                addVM(VMClone(vm, value, vm->ip));
                vm->ip += arg;
                break;
            }
            return;

        case OP_RETURN:
            assert(IVSize(&vm->callStack));
            popStackFrame(vm, &vm->ip, &vm->bp, (uint)arg);
            break;

        case OP_RETURN_VOID:
            if (!IVSize(&vm->callStack))
            {
                VMHalt(vm, 0);
                return;
            }
            popStackFrame(vm, &vm->ip, &vm->bp, 0);
            break;

        case OP_INVOKE:
        {
            vref *values;
            function = *vm->ip++;
            values = (vref*)IVGetAppendPointer(&vm->stack, (size_t)arg);
            for (i = 0; i < arg; i++)
            {
                *values++ = loadValue(vm, vm->bp, *vm->ip++);
            }
            IVAdd(&vm->callStack, (int)(vm->ip - vmBytecode));
            IVAdd(&vm->callStack, vm->bp);
            initStackFrame(vm, &vm->ip, &vm->bp, function, (uint)arg);
            break;
        }

        case OP_INVOKE_NATIVE:
            nativeFunction = refFromInt(arg);
            value = NativeInvoke(vm, nativeFunction);
            storeValue(vm, vm->bp, *vm->ip++, value);
            return;

        case OP_FUNCTION:
        case OP_FUNCTION_UNLINKED:
        case OP_LOAD_FIELD:
        case OP_STORE_FIELD:
        case OP_ITER_NEXT_INDEXED:
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
    bool idle = false;

    IVInit(&temp, 16);
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
            if (VWait(&vm->condition) && VIsFalsy(vm->condition))
            {
                removeVM(i--);
            }
            else if (vm->active)
            {
                execute(vm);
                idle = false;
            }
        }
    }
    while (vmCount > 1 || vmTable[0]->active);

    while (!WorkQueueEmpty() && WorkExecute());

    vm = vmTable[0];
    if (vm->failMessage)
    {
        const char *filename;
        int line = BytecodeLineNumber(program->lineNumbers, (int)(vm->ip - vmBytecode), &filename);
        char *msg = HeapGetStringCopy(vm->failMessage);
        printf("%s:%d: %s\n", filename, line, msg);
#ifdef VALGRIND
        free(msg);
#endif
    }

#ifdef VALGRIND
    removeVM(0);
    free(vmTable);
    IVDispose(&temp);
#endif
}
