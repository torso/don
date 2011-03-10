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


void FunctionIndexInit(void)
{
    ByteVectorInit(&functionTable, 16384);
    /* Position 0 is reserved to mean invalid function. */
    ByteVectorSetSize(&functionTable, sizeof(int));
    IntVectorInit(&localNames);
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

    ByteVectorGrowZero(&functionTable, sizeof(FunctionInfo));
    lastFunction = sizeFromRef(function);
    info = getFunctionInfo(function);
    info->name = name;
    info->file = file;
    info->line = line;
    info->fileOffset = fileOffset;
    return function;
}

void FunctionIndexAddParameter(functionref function, stringref name,
                               fieldref value, boolean vararg)
{
    FunctionInfo *info;
    ParameterInfo *paramInfo;
    size_t parameterInfoOffset = ByteVectorSize(&functionTable);

    ByteVectorGrow(&functionTable, sizeof(ParameterInfo));
    info = getFunctionInfo(function);
    paramInfo = (ParameterInfo*)ByteVectorGetPointer(&functionTable,
                                                     parameterInfoOffset);

    if (!info->parameterCount)
    {
        info->parameterInfoOffset = parameterInfoOffset;
    }
    assert(&FunctionIndexGetParameterInfo(function)[info->parameterCount] ==
           paramInfo);
    info->parameterCount++;
    if (vararg)
    {
        assert(!info->vararg);
        info->vararg = info->parameterCount;
    }
    paramInfo->name = name;
    paramInfo->value = value;
}

void FunctionIndexFinishParameters(functionref function,
                                   uint line, uint fileOffset)
{
    FunctionInfo *info = getFunctionInfo(function);
    info->line = line;
    info->fileOffset = fileOffset;
}

void FunctionIndexSetFailedDeclaration(functionref function)
{
    getFunctionInfo(function)->line = 0;
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
    functionref prevFunction = 0;
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

void FunctionIndexSetBytecodeOffset(functionref function, size_t offset)
{
    assert(offset <= UINT_MAX - 1);
    getFunctionInfo(function)->bytecodeOffset = (uint)offset;
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

void FunctionIndexSetLocals(functionref function, const inthashmap *locals,
                            uint count)
{
    uint offset = (uint)IntVectorSize(&localNames);
    inthashmapiterator iter;
    uint name;
    uint index;
    FunctionInfo *info = getFunctionInfo(function);

    assert(isValidFunction(function));
    assert(count >= IntHashMapSize(locals));

    IntVectorSetSize(&localNames, offset + count);
    info->localCount = count;
    info->localNamesOffset = offset;
    IntHashMapIteratorInit(locals, &iter);
    while (IntHashMapIteratorNext(&iter, &name, &index))
    {
        IntVectorSet(&localNames, offset + index - 1, name);
    }
}
