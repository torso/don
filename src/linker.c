#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "bytecode.h"
#include "fail.h"
#include "inthashmap.h"
#include "linker.h"
#include "log.h"
#include "namespace.h"
#include "native.h"
#include "parser.h"

static const boolean DEBUG_LINKER = false;

typedef struct
{
    intvector out;
    size_t functionStart;
    int smallestConstant;
    int variableCount;
    inthashmap variables;
    intvector jumps;
    int *jumpTargetTable;

    vref filename;
    uint line;
    namespaceref ns;
    boolean hasErrors;
} LinkState;

static void error(LinkState *state, const char *message)
{
    state->hasErrors = true;
    fprintf(stderr, "%s:%u: %s\n", HeapGetString(state->filename), state->line, message);
}

static attrprintf(2, 3) void errorf(LinkState *state, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    state->hasErrors = true;
    fprintf(stderr, "%s:%u: ", HeapGetString(state->filename), state->line);
    vfprintf(stderr, format, args);
    fputs("\n", stderr);
    va_end(args);
}

static int lookupFunction(LinkState *state, namespaceref explicitNS, vref name)
{
    int function;
    if (explicitNS)
    {
        function = NamespaceGetFunction(explicitNS, name);
        if (function < 0)
        {
            errorf(state, "Unknown function '%s.%s'",
                   HeapGetString(NamespaceGetName(explicitNS)),
                   HeapGetString(name));
        }
    }
    else
    {
        function = NamespaceLookupFunction(state->ns, name);
        if (function < 0)
        {
            errorf(state, "Unknown function '%s'", HeapGetString(name));
        }
    }
    return function;
}

static void finishFunction(LinkState *state)
{
    if (IVSize(&state->out))
    {
        const int *jump = IVGetPointer(&state->jumps, 0);
        const int *limit = jump + IVSize(&state->jumps);
        for (; jump < limit; jump++)
        {
            int offset = *jump;
            int *pinstruction = IVGetWritePointer(&state->out, (size_t)offset);
            int instruction = *pinstruction;
            int target = state->jumpTargetTable[(uint)instruction >> 8];
            *pinstruction = (instruction & 0xff) | ((target - offset - 2) << 8);
        }

        IVSet(&state->out, state->functionStart, OP_FUNCTION | (state->variableCount << 8));
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

static void linkVariables(LinkState *state, const int **read, uint count)
{
    while (count--)
    {
        IVAdd(&state->out, linkVariable(state, *(*read)++));
    }
}

boolean Link(ParsedProgram *parsed, LinkedProgram *linked)
{
    LinkState state;
    int *unlinkedFunctions;
    int *currentUnlinkedFunction;
    size_t parsedSize = IVSize(&parsed->bytecode);
    const int *start = IVGetPointer(&parsed->bytecode, 0);
    const int *read = start;
    const int *limit = start + parsedSize;
    int *currentFunction;

    linked->functions = (int*)malloc(IVSize(&parsed->functions) * sizeof(*linked->functions));
    currentFunction = linked->functions;
    unlinkedFunctions = (int*)malloc(parsed->invocationCount * sizeof(*unlinkedFunctions));
    currentUnlinkedFunction = unlinkedFunctions;

    IVInit(&state.out, parsedSize);
    IVInit(&state.jumps, 128);
    IntHashMapInit(&state.variables, 16);
    state.jumpTargetTable = (int*)malloc(parsed->maxJumpTargetCount *
                                         sizeof(*state.jumpTargetTable));

    state.smallestConstant = -(int)IVSize(&parsed->constants);
    state.hasErrors = false;

    while (read < limit)
    {
        int i = *read;
        int arg = i >> 8;
        if (DEBUG_LINKER)
        {
            printf(" link %ld: %d %d: ", read - start, i & 0xff, i >> 8);
            BytecodeDisassembleInstruction(read, start);
        }
        read++;
        switch ((Instruction)(i & 0xff))
        {
        case OP_FILE:
            state.filename = refFromInt(arg);
            state.ns = refFromInt(*read++);
            state.line = 1;
            break;

        case OP_LINE:
            state.line = (uint)arg;
            break;

        case OP_ERROR:
            error(&state, HeapGetString(refFromInt(arg)));
            break;

        case OP_FUNCTION_UNLINKED:
        {
            int param;

            finishFunction(&state);

            *currentFunction++ = (int)IVSize(&state.out);
            IntHashMapClear(&state.variables);
            IVSetSize(&state.jumps, 0);
            state.functionStart = IVSize(&state.out);
            IVAdd(&state.out, OP_FUNCTION);
            state.variableCount = *read++;
            assert(state.variableCount >= 0);
            read++; /* vararg index */
            for (param = 0; param < state.variableCount; param++)
            {
                int name = *read++;
                read++; /* default value */

                if (NamespaceGetField(state.ns, refFromInt(name)) >= 0)
                {
                    errorf(&state, "'%s' is a global variable", HeapGetString(refFromInt(name)));
                }
                else if (IntHashMapSet(&state.variables, name, param + 1))
                {
                    errorf(&state, "Multiple uses of parameter name '%s'",
                           HeapGetString(refFromInt(name)));
                }
            }
            break;
        }
        case OP_NULL:
        case OP_TRUE:
        case OP_FALSE:
        case OP_EMPTY_LIST:
            IVAdd(&state.out, (i & 0xff) | (linkVariable(&state, arg) << 8));
            break;
        case OP_LIST:
            IVAdd(&state.out, i);
            linkVariables(&state, &read, (uint)arg + 1);
            break;
        case OP_FILELIST:
            IVAdd(&state.out, i);
            IVAdd(&state.out, linkVariable(&state, *read++));
            break;
        case OP_STORE_CONSTANT:
            IVAdd(&state.out, (i & 0xff) | (linkVariable(&state, arg) << 8));
            IVAdd(&state.out, *read++);
            break;
        case OP_COPY:
        case OP_NOT:
        case OP_NEG:
        case OP_INV:
            IVAdd(&state.out, (i & 0xff) | (linkVariable(&state, arg) << 8));
            IVAdd(&state.out, linkVariable(&state, *read++));
            break;
        case OP_ITER_GET:
            IVAdd(&state.out, (i & 0xff) | (linkVariable(&state, arg) << 8));
            IVAdd(&state.out, linkVariable(&state, *read++));
            IVAdd(&state.out, linkVariable(&state, *read++));
            IVAdd(&state.out, linkVariable(&state, *read++));
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
            IVAdd(&state.out, (i & 0xff) | (linkVariable(&state, arg) << 8));
            IVAdd(&state.out, linkVariable(&state, *read++));
            IVAdd(&state.out, linkVariable(&state, *read++));
            break;
        case OP_JUMPTARGET:
            state.jumpTargetTable[arg] = (int)IVSize(&state.out);
            break;
        case OP_JUMP_INDEXED:
            IVAdd(&state.jumps, (int)IVSize(&state.out));
            IVAdd(&state.out, i - 1);
            break;
        case OP_BRANCH_TRUE_INDEXED:
        case OP_BRANCH_FALSE_INDEXED:
            IVAdd(&state.jumps, (int)IVSize(&state.out));
            IVAdd(&state.out, i - 1);
            IVAdd(&state.out, linkVariable(&state, *read++));
            break;
        case OP_RETURN:
            IVAdd(&state.out, OP_RETURN | (arg << 8));
            linkVariables(&state, &read, (uint)arg);
            break;
        case OP_RETURN_VOID:
            IVAdd(&state.out, OP_RETURN_VOID);
            break;
        case OP_INVOKE_UNLINKED:
        {
            namespaceref explicitNS = refFromInt(*read++);
            int function = lookupFunction(&state, explicitNS, refFromInt(arg));
            int functionOffset;
            int parameterCount;
            int varargIndex;
            const int *parameters;
            int argumentCount = *read++;
            int returnValueCount;
            int varargValue = 0;
            int index;
            int stop;
            const int *argReadStop;
            size_t argWriteStart;

            assert(argumentCount >= 0);

            if (function < 0)
            {
                read += argumentCount * 2;
                returnValueCount = *read++;
                read += returnValueCount;
                break;
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
                    IVAdd(&state.out, OP_LIST | (length << 8));
                    for (index = 0; index < length; index++)
                    {
                        IVAdd(&state.out,
                              linkVariable(&state, read[(varargIndex + index) * 2 + 1]));
                    }
                    varargValue = state.variableCount++;
                    IVAdd(&state.out, varargValue);
                }
            }

            IVAdd(&state.out,
                  OP_INVOKE | (parameterCount << 8));
            *currentUnlinkedFunction++ = (int)IVSize(&state.out);
            IVAdd(&state.out, function);
            argWriteStart = IVSize(&state.out);
            argReadStop = read + argumentCount * 2;
            for (index = 0, stop = min(min(argumentCount, parameterCount), varargIndex);
                 index < stop && !*read; index++)
            {
                read++;
                IVAdd(&state.out, linkVariable(&state, *read++));
            }
            if (argumentCount > index && varargIndex == INT_MAX && !*read)
            {
                error(&state, "Too many arguments");
            }
            for (; index < parameterCount; index++)
            {
                IVAdd(&state.out, INT_MAX);
            }
            if (varargIndex != INT_MAX)
            {
                IVSet(&state.out, argWriteStart + (size_t)varargIndex, varargValue);
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
                for (index = 0; index < parameterCount; index++)
                {
                    if (parameters[index * 2] == name)
                    {
                        if (IVGet(&state.out, argWriteStart + (size_t)index) == INT_MAX)
                        {
                            IVSet(&state.out, argWriteStart + (size_t)index,
                                  linkVariable(&state, value));
                        }
                        else
                        {
                            errorf(&state, "Parameter '%s' already has a value",
                                   HeapGetString(refFromInt(name)));
                        }
                        goto found;
                    }
                }
                errorf(&state, "No parameter with name '%s'", HeapGetString(refFromInt(name)));
            }
            for (index = 0; index < parameterCount; index++)
            {
                if (IVGet(&state.out, argWriteStart + (size_t)index) == INT_MAX)
                {
                    int value = parameters[index * 2 + 1];
                    if (refFromInt(value) == INT_MAX)
                    {
                        errorf(&state, "No value for parameter '%s'",
                               HeapGetString(refFromInt(parameters[index * 2])));
                    }
                    IVSet(&state.out, argWriteStart + (size_t)index, value);
                }
            }
            returnValueCount = *read++;
            assert(returnValueCount >= 0);
            IVAdd(&state.out, returnValueCount);
            linkVariables(&state, &read, (uint)returnValueCount);
            break;
        }
        case OP_INVOKE_NATIVE:
        {
            nativefunctionref nativeFunction = refFromInt(arg);
            uint argumentCount = NativeGetParameterCount(nativeFunction);
            uint returnValueCount = NativeGetReturnValueCount(nativeFunction);
            IVAdd(&state.out, OP_INVOKE_NATIVE | (arg << 8));
            linkVariables(&state, &read, argumentCount + returnValueCount);
            break;
        }

        case OP_FUNCTION:
        case OP_JUMP:
        case OP_BRANCH_TRUE:
        case OP_BRANCH_FALSE:
        case OP_INVOKE:
        case OP_UNKNOWN_VALUE:
        default:
            assert(false);
        }
    }
    if (state.hasErrors)
    {
        return false;
    }

    finishFunction(&state);

    IVDispose(&parsed->bytecode);
    IVDispose(&parsed->functions);
    IVDispose(&state.jumps);
    IntHashMapDispose(&state.variables);
    free(state.jumpTargetTable);

    if (IVSize(&state.out) >= INT_MAX)
    {
        Fail("Build script too big\n");
        return false;
    }
    linked->size = (uint)IVSize(&state.out);
    linked->bytecode = IVDisposeContainer(&state.out);
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
