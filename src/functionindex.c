#include <memory.h>
#include "builder.h"
#include "intvector.h"
#include "inthashmap.h"
#include "functionindex.h"

#define TABLE_ENTRY_NAME 0
#define TABLE_ENTRY_FILE 1
#define TABLE_ENTRY_LINE 2
#define TABLE_ENTRY_FILE_OFFSET 3
#define TABLE_ENTRY_FLAGS 4
#define TABLE_ENTRY_BYTECODE_OFFSET 5
#define TABLE_ENTRY_PARAMETER_COUNT 6
#define TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT 7
#define TABLE_ENTRY_LOCALS 8
#define TABLE_ENTRY_LOCAL_NAMES_OFFSET 9
#define TABLE_ENTRY_SIZE 10

#define FUNCTION_FLAG_FUNCTION 1
#define FUNCTION_FLAG_QUEUED 2

static intvector functionInfo;
static inthashmap functionIndex;
static uint functionCount;
static boolean hasIndex;
static intvector localNames;

/*
  This value is used temporarily between FunctionIndexBeginFunction and
  FunctionIndexFinishFunction.
*/
static functionref currentFunction;


static uint getFunctionSize(functionref function)
{
    return TABLE_ENTRY_SIZE +
        IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_PARAMETER_COUNT);
}

static boolean isValidFunction(functionref function)
{
    uint offset;
    if (!function || !functionCount || (uint)function >= IntVectorSize(&functionInfo))
    {
        return false;
    }
    for (offset = 1;
         offset < IntVectorSize(&functionInfo);
         offset += getFunctionSize((functionref)offset))
    {
        if (offset >= (uint)function)
        {
            return offset == (uint)function ? true : false;
        }
    }
    return false;
}

static uint getFlag(functionref function, uint flag)
{
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_FLAGS) & flag;
}

static void setFlag(functionref function, uint flag)
{
    IntVectorSet(
        &functionInfo,
        (uint)function + TABLE_ENTRY_FLAGS,
        IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_FLAGS) | flag);
}

ErrorCode FunctionIndexInit(void)
{
    ErrorCode error;

    error = IntVectorInit(&functionInfo);
    if (error)
    {
        return error;
    }
    /* Position 0 is reserved to mean invalid function. */
    error = IntVectorAdd(&functionInfo, 0);
    if (error)
    {
        return error;
    }

    error = IntVectorInit(&localNames);

    functionCount = 0;
    hasIndex = false;
    return error;
}

void FunctionIndexDispose(void)
{
    IntVectorDispose(&functionInfo);
    IntVectorDispose(&localNames);
    if (hasIndex)
    {
        IntHashMapDispose(&functionIndex);
    }
}

boolean FunctionIndexBuildIndex(void)
{
    /* uint tableSize; */
    functionref function;

    assert(!hasIndex);
    if (IntHashMapInit(&functionIndex, FunctionIndexGetFunctionCount()))
    {
        return false;
    }
    hasIndex = true;

    for (function = FunctionIndexGetFirstFunction();
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        IntHashMapAdd(&functionIndex, (uint)FunctionIndexGetName(function), (uint)function);
    }
    return true;
}

functionref FunctionIndexGetFirstFunction(void)
{
    return functionCount ? 1 : 0;
}

functionref FunctionIndexGetNextFunction(functionref function)
{
    assert(isValidFunction(function));
    function = (functionref)((uint)function + getFunctionSize(function));
    return (uint)function < IntVectorSize(&functionInfo) ? function : 0;
}

ErrorCode FunctionIndexBeginFunction(stringref name)
{
    ErrorCode error;

    assert(!hasIndex);
    currentFunction = (functionref)IntVectorSize(&functionInfo);
    error = IntVectorAdd(&functionInfo, (uint)name);
    if (error)
    {
        return error;
    }
    error = IntVectorGrowZero(&functionInfo, TABLE_ENTRY_SIZE - 1);
    if (error)
    {
        return error;
    }
    functionCount++;
    return NO_ERROR;
}

ErrorCode FunctionIndexAddParameter(stringref name, boolean required)
{
    uint parameterCount =
        IntVectorGet(&functionInfo,
                     (uint)currentFunction + TABLE_ENTRY_PARAMETER_COUNT);
    uint minArgumentCount =
        IntVectorGet(&functionInfo,
                     (uint)currentFunction + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT);

    assert(!hasIndex);
    assert(!required || parameterCount == minArgumentCount);
    IntVectorSet(&functionInfo, (uint)currentFunction + TABLE_ENTRY_PARAMETER_COUNT,
                 parameterCount + 1);
    if (required)
    {
        IntVectorSet(&functionInfo,
                     (uint)currentFunction + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT,
                     minArgumentCount + 1);
    }
    return IntVectorAdd(&functionInfo, (uint)name);
}

void FunctionIndexFinishFunction(fileref file, uint line, uint fileOffset,
                                 boolean isTarget)
{
    assert(!isTarget ||
           !IntVectorGet(&functionInfo,
                         (uint)currentFunction + TABLE_ENTRY_PARAMETER_COUNT));
    IntVectorSet(&functionInfo, (uint)currentFunction + TABLE_ENTRY_FILE, file);
    IntVectorSet(&functionInfo, (uint)currentFunction + TABLE_ENTRY_LINE, line);
    IntVectorSet(&functionInfo, (uint)currentFunction + TABLE_ENTRY_FILE_OFFSET,
                 fileOffset);
    if (isTarget)
    {
        setFlag(currentFunction, FUNCTION_FLAG_FUNCTION);
    }
}

uint FunctionIndexGetFunctionCount(void)
{
    return functionCount;
}

functionref FunctionIndexGet(stringref name)
{
    assert(hasIndex);
    return (functionref)IntHashMapGet(&functionIndex, (uint)name);
}

functionref FunctionIndexGetFunctionFromBytecode(uint bytecodeOffset)
{
    functionref function;
    for (function = FunctionIndexGetFirstFunction();
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        if (FunctionIndexGetBytecodeOffset(function) == bytecodeOffset)
        {
            return function;
        }
    }
    return 0;
}

stringref FunctionIndexGetName(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return (stringref)IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_NAME);
}

fileref FunctionIndexGetFile(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_FILE);
}

uint FunctionIndexGetLine(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_LINE);
}

uint FunctionIndexGetFileOffset(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_FILE_OFFSET);
}

boolean FunctionIndexIsTarget(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return getFlag(function, FUNCTION_FLAG_FUNCTION) ? true : false;
}

uint FunctionIndexGetBytecodeOffset(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_BYTECODE_OFFSET);
}

void FunctionIndexSetBytecodeOffset(functionref function, uint offset)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    IntVectorSet(&functionInfo, (uint)function + TABLE_ENTRY_BYTECODE_OFFSET,
                 offset);
}

uint FunctionIndexGetParameterCount(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_PARAMETER_COUNT);
}

const stringref *FunctionIndexGetParameterNames(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return (stringref*)IntVectorGetPointer(&functionInfo, (uint)function + TABLE_ENTRY_SIZE);
}

uint FunctionIndexGetMinimumArgumentCount(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_MINIMUM_ARGUMENT_COUNT);
}

uint FunctionIndexGetLocalsCount(functionref function)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    return IntVectorGet(&functionInfo, (uint)function + TABLE_ENTRY_LOCALS);
}

stringref FunctionIndexGetLocalName(functionref function, uint local)
{
    assert(hasIndex);
    assert(isValidFunction(function));
    assert(local < FunctionIndexGetLocalsCount(function));
    return (stringref)IntVectorGet(
        &localNames,
        IntVectorGet(&functionInfo,
                     (uint)function + TABLE_ENTRY_LOCAL_NAMES_OFFSET));
}

ErrorCode FunctionIndexSetLocals(functionref function, const inthashmap *locals)
{
    uint count = IntHashMapSize(locals);
    uint offset = IntVectorSize(&localNames);
    inthashmapiterator iter;
    uint name;
    uint index;
    ErrorCode error;

    assert(hasIndex);
    assert(isValidFunction(function));

    error = IntVectorSetSize(&localNames, IntVectorSize(&localNames) + count);
    if (error)
    {
        return error;
    }
    IntVectorSet(&functionInfo, (uint)function + TABLE_ENTRY_LOCALS, count);
    IntHashMapIteratorInit(locals, &iter);
    while (IntHashMapIteratorNext(&iter, &name, &index))
    {
        IntVectorSet(&localNames, offset + index - 1, name);
    }
    return NO_ERROR;
}
