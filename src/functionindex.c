#include <memory.h>
#include "common.h"
#include "bytevector.h"
#include "inthashmap.h"
#include "intvector.h"
#include "functionindex.h"

typedef struct
{
    stringref name;
    fileref file;
    uint line;
    uint fileOffset;

    uint bytecodeOffset;
    uint parameterNamesOffset;
    uint parameterCount;
    uint minArgumentCount;
    uint localCount;
    uint localNamesOffset;
} FunctionInfo;

static bytevector functionTable;
static intvector localNames;

/*
  This value is used temporarily between FunctionIndexBeginFunction and
  FunctionIndexFinishFunction.
*/
static functionref currentFunction;


static size_t getFunctionInfoSize(functionref function)
{
    return sizeof(FunctionInfo) +
        ((FunctionInfo*)ByteVectorGetPointer(
            &functionTable, sizeFromRef(function)))->parameterCount *
        (uint)sizeof(ref_t);
}

static boolean isValidFunction(functionref function)
{
    functionref f;
    if (sizeFromRef(function) + sizeof(FunctionInfo) >
        ByteVectorSize(&functionTable))
    {
        return false;
    }
    for (f = FunctionIndexGetFirstFunction();
         f;
         f = refFromSize(sizeFromRef(f) + getFunctionInfoSize(f)))
    {
        if (f == function)
        {
            return true;
        }
    }
    return false;
}

static FunctionInfo *getFunctionInfo(functionref function)
{
    assert(isValidFunction(function));
    return (FunctionInfo*)ByteVectorGetPointer(&functionTable,
                                               sizeFromRef(function));
}


ErrorCode FunctionIndexInit(void)
{
    ErrorCode error;

    error = ByteVectorInit(&functionTable);
    if (error)
    {
        return error;
    }
    /* Position 0 is reserved to mean invalid function. */
    error = ByteVectorSetSize(&functionTable, sizeof(int));
    if (error)
    {
        return error;
    }
    return IntVectorInit(&localNames);
}

void FunctionIndexDispose(void)
{
    ByteVectorDispose(&functionTable);
    IntVectorDispose(&localNames);
}


ErrorCode FunctionIndexBeginFunction(stringref name)
{
    ErrorCode error;
    currentFunction = refFromSize(ByteVectorSize(&functionTable));
    error = ByteVectorGrowZero(&functionTable, sizeof(FunctionInfo));
    if (error)
    {
        return error;
    }
    getFunctionInfo(currentFunction)->name = name;
    return NO_ERROR;
}

ErrorCode FunctionIndexAddParameter(stringref name, boolean required)
{
    FunctionInfo *info = getFunctionInfo(currentFunction);

    assert(!required || info->parameterCount == info->minArgumentCount);
    info->parameterCount++;
    if (required)
    {
        info->minArgumentCount++;
    }
    return ByteVectorAddRef(&functionTable, name);
}

functionref FunctionIndexFinishFunction(fileref file, uint line, uint fileOffset)
{
    FunctionInfo *info = getFunctionInfo(currentFunction);
    info->file = file;
    info->line = line;
    info->fileOffset = fileOffset;
    return currentFunction;
}


functionref FunctionIndexGetFirstFunction(void)
{
    if (ByteVectorSize(&functionTable) == sizeof(int))
    {
        return 0;
    }
    return refFromSize(sizeof(int));
}

functionref FunctionIndexGetNextFunction(functionref function)
{
    function = refFromSize(sizeFromRef(function) +
                           getFunctionInfoSize(function));
    assert(sizeFromRef(function) <= ByteVectorSize(&functionTable));
    if (sizeFromRef(function) == ByteVectorSize(&functionTable))
    {
        return 0;
    }
    return function;
}


functionref FunctionIndexGetFunctionFromBytecode(uint bytecodeOffset)
{
    functionref function;
    functionref lastFunction;
    for (function = FunctionIndexGetFirstFunction();
         function;
         function = FunctionIndexGetNextFunction(function))
    {
        if (FunctionIndexGetBytecodeOffset(function) >= bytecodeOffset)
        {
            if (FunctionIndexGetBytecodeOffset(function) == bytecodeOffset)
            {
                return function;
            }
            return lastFunction;
        }
        lastFunction = function;
    }
    return lastFunction;
}

stringref FunctionIndexGetName(functionref function)
{
    return getFunctionInfo(function)->name;
}

fileref FunctionIndexGetFile(functionref function)
{
    return getFunctionInfo(function)->file;
}

uint FunctionIndexGetLine(functionref function)
{
    return getFunctionInfo(function)->line;
}

uint FunctionIndexGetFileOffset(functionref function)
{
    return getFunctionInfo(function)->fileOffset;
}

uint FunctionIndexGetBytecodeOffset(functionref function)
{
    return getFunctionInfo(function)->bytecodeOffset;
}

void FunctionIndexSetBytecodeOffset(functionref function, uint offset)
{
    getFunctionInfo(function)->bytecodeOffset = offset;
}

uint FunctionIndexGetParameterCount(functionref function)
{
    return getFunctionInfo(function)->parameterCount;
}

const stringref *FunctionIndexGetParameterNames(functionref function)
{
    assert(isValidFunction(function));
    return (stringref*)ByteVectorGetPointer(
        &functionTable, sizeFromRef(function) + sizeof(FunctionInfo));
}

uint FunctionIndexGetMinimumArgumentCount(functionref function)
{
    return getFunctionInfo(function)->minArgumentCount;
}

uint FunctionIndexGetLocalsCount(functionref function)
{
    return getFunctionInfo(function)->localCount;
}

stringref FunctionIndexGetLocalName(functionref function, uint16 local)
{
    assert(local < FunctionIndexGetLocalsCount(function));
    return IntVectorGetRef(&localNames,
                           getFunctionInfo(function)->localNamesOffset);
}

ErrorCode FunctionIndexSetLocals(functionref function, const inthashmap *locals,
                                 uint count)
{
    uint offset = (uint)IntVectorSize(&localNames);
    inthashmapiterator iter;
    uint name;
    uint index;
    FunctionInfo *info = getFunctionInfo(function);
    ErrorCode error;

    assert(isValidFunction(function));
    assert(count >= IntHashMapSize(locals));

    error = IntVectorSetSize(&localNames, offset + count);
    if (error)
    {
        return error;
    }
    info->localCount = count;
    info->localNamesOffset = offset;
    IntHashMapIteratorInit(locals, &iter);
    while (IntHashMapIteratorNext(&iter, &name, &index))
    {
        IntVectorSet(&localNames, offset + index - 1, name);
    }
    return NO_ERROR;
}
