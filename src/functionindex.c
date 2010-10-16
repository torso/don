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
    size_t parameterInfoOffset;
    uint parameterCount;
    uint vararg;
    uint localCount;
    uint localNamesOffset;
} FunctionInfo;

static bytevector functionTable;
static intvector localNames;
static size_t lastFunction = 0;


static boolean isValidFunction(functionref function)
{
    if (sizeFromRef(function) > lastFunction)
    {
        return false;
    }
    return (sizeFromRef(function) - FunctionIndexGetFirstFunction()) %
        sizeof(FunctionInfo) == 0;
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

    error = ByteVectorInit(&functionTable, 16384);
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


functionref FunctionIndexAddFunction(stringref name, fileref file, uint line,
                                     uint fileOffset)
{
    functionref function = refFromSize(ByteVectorSize(&functionTable));
    FunctionInfo *info;

    if (ByteVectorGrowZero(&functionTable, sizeof(FunctionInfo)))
    {
        return 0;
    }
    lastFunction = sizeFromRef(function);
    info = getFunctionInfo(function);
    info->name = name;
    info->file = file;
    info->line = line;
    info->fileOffset = fileOffset;
    return function;
}

ErrorCode FunctionIndexAddParameter(functionref function, stringref name,
                                    fieldref value, boolean vararg)
{
    FunctionInfo *info;
    ParameterInfo *paramInfo;
    size_t parameterInfoOffset = ByteVectorSize(&functionTable);
    ErrorCode error;

    error = ByteVectorGrow(&functionTable, sizeof(ParameterInfo));
    if (error)
    {
        return error;
    }
    info = getFunctionInfo(function);
    paramInfo = (ParameterInfo*)ByteVectorGetPointer(&functionTable,
                                                     parameterInfoOffset);

    if (!info->parameterCount)
    {
        info->parameterInfoOffset = parameterInfoOffset;
    }
    assert(&FunctionIndexGetParameterInfo(function)[info->parameterCount] ==
           paramInfo);
    if (vararg)
    {
        assert(!info->vararg);
        info->vararg = info->parameterCount + 1;
    }
    info->parameterCount++;
    paramInfo->name = name;
    paramInfo->value = value;
    return NO_ERROR;
}

void FunctionIndexFinishParameters(functionref function,
                                   uint line, uint fileOffset)
{
    FunctionInfo *info = getFunctionInfo(function);
    info->line = line;
    info->fileOffset = fileOffset;
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
    assert(isValidFunction(function));
    function = refFromSize(sizeFromRef(function) + sizeof(FunctionInfo));
    assert(lastFunction <= ByteVectorSize(&functionTable));
    if (sizeFromRef(function) > lastFunction)
    {
        return 0;
    }
    return function;
}


functionref FunctionIndexGetFunctionFromBytecode(uint bytecodeOffset)
{
    functionref function;
    functionref prevFunction;
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
            return prevFunction;
        }
        prevFunction = function;
    }
    return prevFunction;
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

const ParameterInfo *FunctionIndexGetParameterInfo(functionref function)
{
    FunctionInfo *info = getFunctionInfo(function);
    return (const ParameterInfo*)ByteVectorGetPointer(
        &functionTable, info->parameterInfoOffset);
}

boolean FunctionIndexHasVararg(functionref function)
{
    return getFunctionInfo(function)->vararg != 0;
}

uint FunctionIndexGetVarargIndex(functionref function)
{
    assert(FunctionIndexHasVararg(function));
    return getFunctionInfo(function)->vararg - 1;
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
