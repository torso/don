#include "config.h"
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "bytecode.h"
#include "debug.h"
#include "heap.h"
#include "interpreter.h"
#include "instruction.h"
#include "job.h"
#include "linker.h"
#include "main.h"
#include "native.h"
/* #include "value.h" */
#include "vm.h"

static intvector temp;

static void traceLine(const VM* vm, int bytecodeOffset)
{
    const char *filename;
    int line = BytecodeLineNumber(vmLineNumbers, bytecodeOffset, &filename);
    printf("[%p] %s:%d: %d: ", (void*)vm, filename, line, bytecodeOffset);
    BytecodeDisassembleInstruction(vmBytecode + bytecodeOffset, vmBytecode);
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
    if (DEBUG_TRACE)
    {
        traceLine(vm, functionOffset);
    }
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


static VM *execute(VM *vm)
{
    int maxInstructions = 100;

    while (maxInstructions--)
    {
        int i = *vm->ip;
        int arg = i >> 8;
        if (DEBUG_TRACE)
        {
            traceLine(vm, (int)(vm->ip - vmBytecode));
            fflush(stdout);
        }
        vm->ip++;
        switch ((Instruction)(i & 0xff))
        {
        case OP_NULL:
            storeValue(vm, vm->bp, arg, VNull);
            break;

        case OP_TRUE:
            storeValue(vm, vm->bp, arg, VTrue);
            break;

        case OP_FALSE:
            storeValue(vm, vm->bp, arg, VFalse);
            break;

        case OP_EMPTY_LIST:
            storeValue(vm, vm->bp, arg, VEmptyList);
            break;

        case OP_LIST:
        {
            vref result;
            vref *array;
            vref *write;
            assert(arg);
            array = VCreateArray((size_t)arg);
            for (write = array; arg--; write++)
            {
                vref value = loadValue(vm, vm->bp, *vm->ip++);
                if (value == VFuture)
                {
                    vm->ip += arg;
                    VAbortArray(array);
                    result = VFuture;
                    goto storeList;
                }
                *write = value;
                assert(HeapGetObjectType(*write) != TYPE_FUTURE);
            }
            result = VFinishArray(array);
    storeList:
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_FILELIST:
        {
            vref string = refFromInt(arg);
            vref result;
            if (string == VFuture)
            {
                result = VFuture;
            }
            else
            {
                result = VCreateFilelistGlob(VGetString(string), VStringLength(string));
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_STORE_CONSTANT:
            storeValue(vm, vm->bp, arg, refFromInt(*vm->ip++));
            break;

        case OP_COPY:
            storeValue(vm, vm->bp, *vm->ip++, loadValue(vm, vm->bp, arg));
            break;

        case OP_NOT:
            storeValue(vm, vm->bp, *vm->ip++,
                       VNot(loadValue(vm, vm->bp, arg)));
            break;

        case OP_NEG:
        {
            vref result = VNeg(vm, loadValue(vm, vm->bp, arg));
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_INV:
        {
            vref result = VInv(vm, loadValue(vm, vm->bp, arg));
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_ITER_NEXT:
        {
            vref collection = loadValue(vm, vm->bp, *vm->ip++);
            int indexVariable = *vm->ip++;
            vref index = loadValue(vm, vm->bp, indexVariable);
            vref step = loadValue(vm, vm->bp, *vm->ip++);
            if (index == VFuture || step == VFuture)
            {
                index = VFuture;
                storeValue(vm, vm->bp, indexVariable, index);
                goto iterNextFuture;
            }
            else
            {
                index = VAdd(vm, index, step);
                storeValue(vm, vm->bp, indexVariable, index);
            }
            if (collection == VFuture)
            {
        iterNextFuture:
                assert(0);
            }
            switch (VGetBool(VValidIndex(vm, collection, index)))
            {
            case TRUTHY:
                storeValue(vm, vm->bp, *vm->ip++, VIndexedAccess(vm, collection, index));
                break;
            case FALSY:
                vm->ip += arg - 2;
                break;
            case FUTURE:
                unreachable;
            }
            break;
        }

        case OP_EQUALS:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VEquals(value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_NOT_EQUALS:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VEquals(value1, value2);
            if (!result)
            {
                return vm;
            }
            switch (VGetBool(result))
            {
            case TRUTHY: result = VFalse; break;
            case FALSY: result = VTrue; break;
            case FUTURE: break;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_LESS_EQUALS:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VLessEquals(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_GREATER_EQUALS:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VLessEquals(vm, value2, value1);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_LESS:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VLess(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_GREATER:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VLess(vm, value2, value1);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_ADD:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VAdd(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_SUB:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VSub(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_MUL:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VMul(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_DIV:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VDiv(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_REM:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VRem(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_CONCAT_LIST:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VConcat(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_CONCAT_STRING:
        {
            vref result;
            assert(!IVSize(&temp));
            for (i = 0; i < arg; i++)
            {
                IVAdd(&temp, intFromRef(loadValue(vm, vm->bp, *vm->ip++)));
            }
            result = VConcatString((size_t)arg, (vref*)IVGetWritePointer(&temp, 0));
            storeValue(vm, vm->bp, *vm->ip++, result);
            IVSetSize(&temp, 0);
            break;
        }

        case OP_INDEXED_ACCESS:
        {
            vref collection = loadValue(vm, vm->bp, arg);
            vref index = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VIndexedAccess(vm, collection, index);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_RANGE:
        {
            vref value1 = loadValue(vm, vm->bp, arg);
            vref value2 = loadValue(vm, vm->bp, *vm->ip++);
            vref result = VRange(vm, value1, value2);
            if (!result)
            {
                return vm;
            }
            storeValue(vm, vm->bp, *vm->ip++, result);
            break;
        }

        case OP_JUMP:
            vm->ip += arg + 1;
            break;

        case OP_BRANCH_TRUE:
        {
            vref value = loadValue(vm, vm->bp, *vm->ip++);
            VBool b = VGetBool(value);
            vm->base.clonePoints++;
            if (vm->child && vm->base.clonePoints >= vm->child->clonePoints)
            {
                if (vm->base.clonePoints > vm->child->clonePoints)
                {
                    VMDispose(vm->child);
                    vm->child = null;
                    goto branchTrueNoChild;
                }
                assert(!vm->child->fullVM);
                if (b == FUTURE)
                {
                    VMReplaceCloneBranch(vm, vm->ip + arg);
                }
                else
                {
                    uint keepChild;
                    if (b == FALSY)
                    {
                        keepChild = 0;
                    }
                    else
                    {
                        vm->ip += arg;
                        keepChild = 1;
                    }
                    vm->child = VMDisposeBranch((VMBranch*)vm->child, keepChild);
                }
            }
            else
            {
        branchTrueNoChild:
                switch (b)
                {
                case FALSY:
                    break;
                case FUTURE:
                    assert(!vm->child);
                    VMCloneBranch(vm, vm->ip);
                    /* fallthrough */
                case TRUTHY:
                    vm->ip += arg;
                    break;
                }
            }
            break;
        }

        case OP_BRANCH_FALSE:
        {
            vref value = loadValue(vm, vm->bp, *vm->ip++);
            VBool b = VGetBool(value);
            vm->base.clonePoints++;
            if (vm->child && vm->base.clonePoints >= vm->child->clonePoints)
            {
                if (vm->base.clonePoints > vm->child->clonePoints)
                {
                    VMDispose(vm->child);
                    vm->child = null;
                    goto branchFalseNoChild;
                }
                assert(!vm->child->fullVM);
                if (b == FUTURE)
                {
                    VMReplaceCloneBranch(vm, vm->ip);
                    vm->ip += arg;
                }
                else
                {
                    uint keepChild;
                    if (b == FALSY)
                    {
                        vm->ip += arg;
                        keepChild = 0;
                    }
                    else
                    {
                        keepChild = 1;
                    }
                    vm->child = VMDisposeBranch((VMBranch*)vm->child, keepChild);
                }
            }
            else
            {
        branchFalseNoChild:
                switch (b)
                {
                case TRUTHY:
                    break;
                case FUTURE:
                    assert(!vm->child);
                    VMCloneBranch(vm, vm->ip);
                    /* fallthrough */
                case FALSY:
                    vm->ip += arg;
                    break;
                }
            }
            break;
        }

        case OP_RETURN:
            assert(IVSize(&vm->callStack));
            popStackFrame(vm, &vm->ip, &vm->bp, (uint)arg);
            break;

        case OP_RETURN_VOID:
            if (!IVSize(&vm->callStack))
            {
                vm->base.clonePoints++;
                VMHalt(vm, 0);
                return vm;
            }
            popStackFrame(vm, &vm->ip, &vm->bp, 0);
            break;

        case OP_INVOKE:
        {
            vref *values;
            int function = *vm->ip++;
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
        {
            nativefunctionref nativeFunction = refFromInt(arg);
            vref value;
            int storeAt;
            assert(!vm->job);
            vm->base.clonePoints++;
            if (vm->child && vm->base.clonePoints >= vm->child->clonePoints)
            {
                VM *child = (VM*)vm->child;
                assert(vm->child->fullVM);
                VMReplaceChild(vm, child);
            }
            value = NativeInvoke(vm, nativeFunction);
            if (vm->idle)
            {
                return vm;
            }
            storeAt = *vm->ip++;
            storeValue(vm, vm->bp, storeAt, value);
            if (vm->job)
            {
                vm->job->storeAt = storeAt;
                vm->idle = true;

                /* TODO: Activate speculative execution */
                JobExecute(vm->job);
                if (unlikely(vm->failMessage))
                {
                    return vm;
                }
                /* vm = VMClone(vm, vm->ip); */
            }
            break;
        }

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
    return vm;
}

void InterpreterExecute(const LinkedProgram *program, int target)
{
    VM *masterVM;

    IVInit(&temp, 16);
    vmBytecode = program->bytecode;
    vmLineNumbers = program->lineNumbers;
    masterVM = VMCreate(program);
    initStackFrame(masterVM, &masterVM->ip, &masterVM->bp, target, 0);

    for (;;)
    {
        VMBase *vmBase = &masterVM->base;
        bool idle = true;

        /* Traverse VMs, letting all non-idle execute for a while. Note that the tree can change as
           a result of executing. */
        for (;;)
        {
            if (vmBase->fullVM)
            {
                VM *vm = (VM*)vmBase;
                if (!vm->idle)
                {
                    /* TODO: Cloning new VMs can cause infinite traversal. */
                    vm = execute(vm);
                    idle = false;
                }
                else if (vm->child)
                {
                    vmBase = vm->child;
                    continue;
                }
                {
                    VMBase *parent = vm->base.parent;
                    VMBase *child = &vm->base;
                    VMBase **p;
                    if (!parent)
                    {
                        break;
                    }
                    while (parent->fullVM ||
                           ((VMBranch*)parent)->children[
                               ((VMBranch*)parent)->childCount - 1] == child)
                    {
                        child = parent;
                        parent = parent->parent;
                        if (!parent)
                        {
                            goto done;
                        }
                    }
                    for (p = ((VMBranch*)parent)->children; *p != child; p++)
                    {
                        assert(p < ((VMBranch*)parent)->children + ((VMBranch*)parent)->childCount);
                    }
                    vmBase = *(p + 1);
                }
            }
            else
            {
                VMBranch *vmBranch = (VMBranch*)vmBase;
                assert(vmBranch->childCount);
                vmBase = vmBranch->children[0];
            }
        }
done:

        if (idle)
        {
            if (masterVM->job)
            {
                JobExecute(masterVM->job);
            }
            else if (masterVM->idle)
            {
                break;
            }
        }
    }

    if (masterVM->failMessage)
    {
        const char *filename;
        int line = BytecodeLineNumber(program->lineNumbers,
                                      (int)(masterVM->ip - vmBytecode), &filename);
        char *msg = VGetStringCopy(masterVM->failMessage);
        fprintf(stderr, "%s:%d: %s\n", filename, line, msg);
#ifdef VALGRIND
        free(msg);
#endif
        cleanShutdown(EXIT_FAILURE);
    }

#ifdef VALGRIND
    VMDispose(&masterVM->base);
    IVDispose(&temp);
#endif
}
