#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include "common.h"
#include "bytecode.h"
#include "debug.h"
#include "fail.h"
#include "heap.h"
#include "instruction.h"
#include "inthashmap.h"
#include "intvector.h"
#include "linker.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"

typedef struct
{
    intvector out;
    size_t functionStart;
    int smallestConstant;
    int variableCount;
    int parameterCount;
    inthashmap variables;
    int *jumps;
    int jumpCount;
    int *jumpTargetTable;

    const char *filename;
    int line;
    namespaceref ns;
    bool hasErrors;
} LinkState;

static void error(LinkState *state, const char *message)
{
    if (DEBUG_LINKER)
    {
        fflush(stdout);
    }
    state->hasErrors = true;
    fprintf(stderr, "%s:%u: %s\n", state->filename, state->line, message);
}

static attrprintf(2, 3) void errorf(LinkState *state, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    state->hasErrors = true;
    fprintf(stderr, "%s:%u: ", state->filename, state->line);
    vfprintf(stderr, format, args);
    fputs("\n", stderr);
    va_end(args);
}

static void finishFunction(LinkState *state)
{
    if (IVSize(&state->out))
    {
        const int *jump = state->jumps;
        const int *limit = jump + state->jumpCount;
        for (; jump < limit; jump++)
        {
            int offset = *jump;
            int *pinstruction = IVGetWritePointer(&state->out, (size_t)offset);
            int instruction = *pinstruction;
            int target = state->jumpTargetTable[(uint)instruction >> 8];
            *pinstruction = (instruction & 0xff) | ((target - offset - 2) << 8);
        }

        IVSet(&state->out, state->functionStart,
              OP_FUNCTION | ((state->variableCount - state->parameterCount) << 8));
    }
}

static int linkVariable(LinkState *state, int variable)
{
    int value;
    if (variable < 0)
    {
        if (variable < state->smallestConstant)
        {
            /* Anonymous local variable */
            value = IntHashMapGet(&state->variables, variable);
            if (value)
            {
                return value - 1;
            }
            value = state->variableCount++;
            IntHashMapAdd(&state->variables, variable, value + 1);
            return value;
        }
        return variable;
    }
    value = IntHashMapGet(&state->variables, variable);
    if (value)
    {
        return value - 1;
    }
    value = NamespaceLookupField(state->ns, refFromInt(variable));
    if (value >= 0)
    {
        return state->smallestConstant - value - 1;
    }
    value = state->variableCount++;
    IntHashMapAdd(&state->variables, variable, value + 1);
    return value;
}

static void linkVariables(LinkState *state, const int **read, int *write, uint count)
{
    while (count--)
    {
        *write++ = linkVariable(state, *(*read)++);
    }
}

bool Link(ParsedProgram *parsed, LinkedProgram *linked)
{
    LinkState state;
    int *unlinkedFunctions;
    int *currentUnlinkedFunction;
    size_t parsedSize = IVSize(&parsed->bytecode);
    const int *start = IVGetPointer(&parsed->bytecode, 0);
    const int *read = start;
    const int *limit = start + parsedSize;
    int *currentFunction;
    int *write;
    intvector lineNumbers;
    size_t lineStart = 0;

    linked->functions = (int*)malloc(IVSize(&parsed->functions) * sizeof(*linked->functions));
    currentFunction = linked->functions;
    unlinkedFunctions = (int*)malloc(parsed->invocationCount * sizeof(*unlinkedFunctions));
    currentUnlinkedFunction = unlinkedFunctions;

    IVInit(&state.out, parsedSize);
    IVInit(&lineNumbers, parsedSize / 2);
    IntHashMapInit(&state.variables, 128);
    state.jumps = (int*)malloc(parsed->maxJumpCount * sizeof(*state.jumps));
    state.jumpTargetTable = (int*)malloc(parsed->maxJumpTargetCount *
                                         sizeof(*state.jumpTargetTable));

    state.smallestConstant = -(int)IVSize(&parsed->constants);
    state.hasErrors = false;

    while (read < limit)
    {
        int i = *read;
        int op = (Instruction)(i & 0xff);
        int arg = i >> 8;
        if (DEBUG_LINKER)
        {
            printf(" link %ld: %d %d: ", read - start, op, i >> 8);
            BytecodeDisassembleInstruction(read, start);
        }
        read++;
        switch ((Instruction)op)
        {
        case OP_FILE:
        {
            size_t newLineStart = IVSize(&state.out);
            int length = *read++;
            state.filename = (const char*)read;
            state.ns = refFromInt(arg);
            state.line = 1;
            read += (length + 4) >> 2;

            if (IVSize(&lineNumbers))
            {
                IVAdd(&lineNumbers, (int)(newLineStart - lineStart));
                IVAdd(&lineNumbers, -1);
            }
            IVAppendString(&lineNumbers, state.filename, (size_t)length);
            IVAdd(&lineNumbers, 1);
            lineStart = newLineStart;
            break;
        }

        case OP_LINE:
        {
            size_t newLineStart = IVSize(&state.out);
            state.line = arg;
            IVAdd(&lineNumbers, (int)(newLineStart - lineStart));
            IVAdd(&lineNumbers, arg);
            lineStart = newLineStart;
            break;
        }

        case OP_ERROR:
            error(&state, VGetString(refFromInt(arg)));
            break;

        case OP_FUNCTION_UNLINKED:
        {
            int param;

            finishFunction(&state);

            *currentFunction++ = (int)IVSize(&state.out);
            IntHashMapClear(&state.variables);
            state.jumpCount = 0;
            state.functionStart = IVSize(&state.out);
            IVAdd(&state.out, OP_FUNCTION);
            state.variableCount = *read++;
            state.parameterCount = state.variableCount;
            assert(state.variableCount >= 0);
            read++; /* vararg index */
            for (param = 0; param < state.variableCount; param++)
            {
                int name = *read++;
                read++; /* default value */

                if (unlikely(NamespaceGetField(state.ns, refFromInt(name)) >= 0))
                {
                    errorf(&state, "'%s' is a global variable", VGetString(refFromInt(name)));
                }
                else if (unlikely(IntHashMapSet(&state.variables, name, param + 1)))
                {
                    errorf(&state, "Multiple uses of parameter name '%s'",
                           VGetString(refFromInt(name)));
                }
            }
            break;
        }
        case OP_NULL:
        case OP_TRUE:
        case OP_FALSE:
        case OP_EMPTY_LIST:
            IVAdd(&state.out, op | (linkVariable(&state, arg) << 8));
            break;
        case OP_LIST:
            write = IVGetAppendPointer(&state.out, (uint)arg + 2);
            *write++ = i;
            linkVariables(&state, &read, write, (uint)arg + 1);
            break;
        case OP_FILELIST:
            write = IVGetAppendPointer(&state.out, 2);
            *write++ = i;
            *write++ = linkVariable(&state, *read++);
            break;
        case OP_STORE_CONSTANT:
            write = IVGetAppendPointer(&state.out, 2);
            *write++ = op | (linkVariable(&state, arg) << 8);
            *write++ = *read++;
            break;
        case OP_COPY:
        case OP_NOT:
        case OP_NEG:
        case OP_INV:
            write = IVGetAppendPointer(&state.out, 2);
            *write++ = op | (linkVariable(&state, arg) << 8);
            *write++ = linkVariable(&state, *read++);
            break;
        case OP_LOAD_FIELD:
        {
            vref nsName = refFromInt(*read++);
            namespaceref ns;
            int field;
            int variable = *read++;
            ns = NamespaceGetNamespace(state.ns, nsName);
            if (unlikely(!ns))
            {
                errorf(&state, "Unknown namespace '%s'", VGetString(nsName));
                break;
            }
            field = NamespaceLookupField(ns, refFromInt(arg));
            if (unlikely(field < 0))
            {
                errorf(&state, "Unknown field '%s.%s'", VGetString(nsName),
                       VGetString(refFromInt(arg)));
                break;
            }
            write = IVGetAppendPointer(&state.out, 2);
            *write++ = OP_COPY | ((state.smallestConstant - field - 1) << 8);
            *write++ = linkVariable(&state, variable);
            break;
        }
        case OP_STORE_FIELD:
        {
            vref nsName = refFromInt(*read++);
            namespaceref ns;
            int field;
            int variable = *read++;
            ns = NamespaceGetNamespace(state.ns, nsName);
            if (unlikely(!ns))
            {
                errorf(&state, "Unknown namespace '%s'", VGetString(nsName));
                break;
            }
            field = NamespaceLookupField(ns, refFromInt(arg));
            if (unlikely(field < 0))
            {
                errorf(&state, "Unknown field '%s.%s'", VGetString(nsName),
                       VGetString(refFromInt(arg)));
                break;
            }
            write = IVGetAppendPointer(&state.out, 2);
            *write++ = OP_COPY | (linkVariable(&state, variable) << 8);
            *write++ = state.smallestConstant - field - 1;
            break;
        }
        case OP_ITER_NEXT_INDEXED:
            state.jumps[state.jumpCount++] = (int)IVSize(&state.out);
            write = IVGetAppendPointer(&state.out, 5);
            *write++ = i - 1;
            *write++ = linkVariable(&state, *read++);
            *write++ = linkVariable(&state, *read++);
            *write++ = linkVariable(&state, *read++);
            *write++ = linkVariable(&state, *read++);
            break;
        case OP_EQUALS:
        case OP_NOT_EQUALS:
        case OP_LESS_EQUALS:
        case OP_GREATER_EQUALS:
        case OP_LESS:
        case OP_GREATER:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_REM:
        case OP_CONCAT_LIST:
        case OP_INDEXED_ACCESS:
        case OP_RANGE:
            write = IVGetAppendPointer(&state.out, 3);
            *write++ = op | (linkVariable(&state, arg) << 8);
            *write++ = linkVariable(&state, *read++);
            *write++ = linkVariable(&state, *read++);
            break;
        case OP_CONCAT_STRING:
            write = IVGetAppendPointer(&state.out, (uint)arg + 2);
            *write++ = i;
            linkVariables(&state, &read, write, (uint)arg + 1);
            break;
        case OP_JUMPTARGET:
            state.jumpTargetTable[arg] = (int)IVSize(&state.out);
            break;
        case OP_JUMP_INDEXED:
            state.jumps[state.jumpCount++] = (int)IVSize(&state.out);
            IVAdd(&state.out, i - 1);
            break;
        case OP_BRANCH_TRUE_INDEXED:
        case OP_BRANCH_FALSE_INDEXED:
            state.jumps[state.jumpCount++] = (int)IVSize(&state.out);
            write = IVGetAppendPointer(&state.out, 2);
            *write++ = i - 1;
            *write++ = linkVariable(&state, *read++);
            break;
        case OP_RETURN:
            write = IVGetAppendPointer(&state.out, (uint)arg + 1);
            *write++ = i;
            linkVariables(&state, &read, write, (uint)arg);
            break;
        case OP_RETURN_VOID:
            IVAdd(&state.out, OP_RETURN_VOID);
            break;
        case OP_INVOKE_UNLINKED:
        {
            vref nsName = refFromInt(*read++);
            namespaceref ns = 0;
            int function;
            int functionOffset;
            int parameterCount;
            int varargIndex;
            const int *parameters;
            int argumentCount = *read++;
            int returnValueCount = *read++;
            int varargValue = 0;
            int index;
            int stop;
            const int *argReadStop;
            int *argWriteStart;

            assert(argumentCount >= 0);
            assert(returnValueCount >= 0);

            if (nsName)
            {
                ns = NamespaceGetNamespace(state.ns, nsName);
                if (unlikely(!ns))
                {
                    errorf(&state, "Unknown namespace '%s'", VGetString(nsName));
                    read += argumentCount * 2 + returnValueCount;
                    break;
                }
                function = NamespaceGetFunction(ns, refFromInt(arg));
                if (unlikely(function < 0))
                {
                    errorf(&state, "Unknown function '%s.%s'",
                           VGetString(nsName), VGetString(refFromInt(arg)));
                }
            }
            else
            {
                function = NamespaceLookupFunction(state.ns, refFromInt(arg));
                if (unlikely(function < 0))
                {
                    errorf(&state, "Unknown function '%s'", VGetString(refFromInt(arg)));
                    read += argumentCount * 2 + returnValueCount;
                    break;
                }
            }

            functionOffset = IVGet(&parsed->functions, (size_t)function);
            parameterCount = start[functionOffset + 1];
            varargIndex = start[functionOffset + 2];
            parameters = start + functionOffset + 3;

            assert(parameterCount >= 0);

            if (varargIndex != INT_MAX)
            {
                if (argumentCount <= varargIndex || read[varargIndex * 2])
                {
                    varargIndex = INT_MAX;
                }
                else
                {
                    int length;
                    for (length = 1; length < argumentCount && !read[(varargIndex + length) * 2];
                         length++);
                    write = IVGetAppendPointer(&state.out, (size_t)(2 + length));
                    *write++ = OP_LIST | (length << 8);
                    for (index = 0; index < length; index++)
                    {
                        *write++ = linkVariable(&state, read[(varargIndex + index) * 2 + 1]);
                    }
                    varargValue = state.variableCount++;
                    *write++ = varargValue;
                }
            }

            *currentUnlinkedFunction++ = (int)IVSize(&state.out) + 1;
            write = IVGetAppendPointer(&state.out, 3 + (size_t)parameterCount +
                                       (size_t)returnValueCount);
            *write++ = OP_INVOKE | (parameterCount << 8);
            *write++ = function;
            argWriteStart = write;
            argReadStop = read + argumentCount * 2;
            for (index = 0, stop = min(min(argumentCount, parameterCount), varargIndex);
                 index < stop && !*read; index++)
            {
                read++;
                *write++ = linkVariable(&state, *read++);
            }
            if (unlikely(argumentCount > index && varargIndex == INT_MAX && !*read))
            {
                error(&state, "Too many arguments");
            }
            for (; index < parameterCount; index++)
            {
                *write++ = INT_MAX;
            }
            if (varargIndex != INT_MAX)
            {
                argWriteStart[varargIndex] = varargValue;
            }
            while (read < argReadStop && !*read)
            {
                read += 2;
            }
    found:
            while (read < argReadStop)
            {
                int name = *read++;
                int value = *read++;
                for (index = 0; likely(index < parameterCount); index++)
                {
                    if (parameters[index * 2] == name)
                    {
                        if (likely(argWriteStart[index] == INT_MAX))
                        {
                            argWriteStart[index] = linkVariable(&state, value);
                        }
                        else
                        {
                            errorf(&state, "Parameter '%s' already has a value",
                                   VGetString(refFromInt(name)));
                        }
                        goto found;
                    }
                }
                errorf(&state, "No parameter with name '%s'", VGetString(refFromInt(name)));
            }
            for (index = 0; index < parameterCount; index++)
            {
                if (argWriteStart[index] == INT_MAX)
                {
                    int value = parameters[index * 2 + 1];
                    if (unlikely(refFromInt(value) == INT_MAX))
                    {
                        errorf(&state, "No value for parameter '%s'",
                               VGetString(refFromInt(parameters[index * 2])));
                    }
                    argWriteStart[index] = value;
                }
            }
            *write++ = returnValueCount;
            linkVariables(&state, &read, write, (uint)returnValueCount);
            break;
        }
        case OP_INVOKE_NATIVE:
        {
            nativefunctionref nativeFunction = refFromInt(arg);
            uint argumentCount = NativeGetParameterCount(nativeFunction);
            write = IVGetAppendPointer(&state.out, argumentCount + 2);
            *write++ = OP_INVOKE_NATIVE | (arg << 8);
            linkVariables(&state, &read, write, argumentCount + 1);
            break;
        }

        case OP_FUNCTION:
        case OP_ITER_NEXT:
        case OP_JUMP:
        case OP_BRANCH_TRUE:
        case OP_BRANCH_FALSE:
        case OP_INVOKE:
        case OP_UNKNOWN_VALUE:
        default:
            unreachable;
        }
    }
    if (state.hasErrors)
    {
        return false;
    }

    finishFunction(&state);

    assert(IVSize(&lineNumbers));
    IVAdd(&lineNumbers, (int)(IVSize(&state.out) - lineStart));

    IVDispose(&parsed->bytecode);
    IVDispose(&parsed->functions);
    free(state.jumps);
    IntHashMapDispose(&state.variables);
    free(state.jumpTargetTable);

    if (IVSize(&state.out) >= INT_MAX)
    {
        Fail("Build script too big\n");
        return false;
    }
    linked->size = (uint)IVSize(&state.out);
    linked->bytecode = IVDisposeContainer(&state.out);
    linked->lineNumbers = IVDisposeContainer(&lineNumbers);
    linked->constantCount = (int)IVSize(&parsed->constants);
    linked->constants = (vref*)IVDisposeContainer(&parsed->constants);
    linked->fieldCount = (int)IVSize(&parsed->fields);
    linked->fields = (vref*)IVDisposeContainer(&parsed->fields);

    for (currentUnlinkedFunction = unlinkedFunctions,
             limit = unlinkedFunctions + parsed->invocationCount;
         currentUnlinkedFunction < limit; currentUnlinkedFunction++)
    {
        linked->bytecode[*currentUnlinkedFunction] =
            linked->functions[linked->bytecode[*currentUnlinkedFunction]];
    }
    free(unlinkedFunctions);

    return true;
}
