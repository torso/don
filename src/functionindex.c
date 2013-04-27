#include <memory.h>
#include "common.h"
#include "bytevector.h"
#include "inthashmap.h"
#include "intvector.h"
#include "functionindex.h"

typedef struct
{
    stringref name;
    namespaceref ns;
    stringref filename;
    uint line;
    uint fileOffset;

    uint bytecodeOffset;
    size_t parameterInfoOffset;
    uint parameterCount;
    uint requiredArgumentCount;
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
    return (sizeFromRef(function) - sizeFromRef(FunctionIndexGetFirstFunction())) %
        sizeof(FunctionInfo) == 0;
}

static FunctionInfo *getFunctionInfo(functionref function)
{
    assert(isValidFunction(function));
    return (FunctionInfo*)BVGetPointer(&functionTable,
                                       sizeFromRef(function));
}


void FunctionIndexInit(void)
{
    BVInit(&functionTable, 16384);
    /* Position 0 is reserved to mean invalid function. */
    BVSetSize(&functionTable, sizeof(int));
    IVInit(&localNames, 128);
}

void FunctionIndexDispose(void)
{
    BVDispose(&functionTable);
    IVDispose(&localNames);
}


functionref FunctionIndexAddFunction(namespaceref ns, stringref name,
                                     stringref filename, uint line, uint fileOffset)
{
    functionref function = refFromSize(BVSize(&functionTable));
    FunctionInfo *info;

    BVGrowZero(&functionTable, sizeof(FunctionInfo));
    lastFunction = sizeFromRef(function);
    info = getFunctionInfo(function);
    info->name = name;
    info->ns = ns;
    info->filename = filename;
    info->line = line;
    info->fileOffset = fileOffset;
    return function;
}

void FunctionIndexAddParameter(functionref function, stringref name,
                               boolean hasValue, objectref value,
                               boolean vararg)
{
    FunctionInfo *info;
    ParameterInfo *paramInfo;
    size_t parameterInfoOffset = BVSize(&functionTable);

    BVGrow(&functionTable, sizeof(ParameterInfo));
    info = getFunctionInfo(function);
    paramInfo = (ParameterInfo*)BVGetPointer(&functionTable,
                                             parameterInfoOffset);

    if (!info->parameterCount)
    {
        info->parameterInfoOffset = parameterInfoOffset;
    }
    assert(&FunctionIndexGetParameterInfo(function)[info->parameterCount] ==
           paramInfo);
    if (!hasValue)
    {
        assert(info->requiredArgumentCount == info->parameterCount);
        info->requiredArgumentCount++;
    }
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
    if (BVSize(&functionTable) == sizeof(int))
    {
        return 0;
    }
    return refFromSize(sizeof(int));
}

functionref FunctionIndexGetNextFunction(functionref function)
{
    assert(isValidFunction(function));
    function = refFromSize(sizeFromRef(function) + sizeof(FunctionInfo));
    assert(lastFunction <= BVSize(&functionTable));
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

namespaceref FunctionIndexGetNamespace(functionref function)
{
    return getFunctionInfo(function)->ns;
}

stringref FunctionIndexGetFilename(functionref function)
{
    return getFunctionInfo(function)->filename;
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

uint FunctionIndexGetRequiredArgumentCount(functionref function)
{
    return getFunctionInfo(function)->requiredArgumentCount;
}

const ParameterInfo *FunctionIndexGetParameterInfo(functionref function)
{
    FunctionInfo *info = getFunctionInfo(function);
    return (const ParameterInfo*)BVGetPointer(
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
    return IVGetRef(&localNames,
                    getFunctionInfo(function)->localNamesOffset);
}

void FunctionIndexSetLocals(functionref function, const inthashmap *locals,
                            uint count)
{
    uint offset = (uint)IVSize(&localNames);
    inthashmapiterator iter;
    uint name;
    uint index;
    FunctionInfo *info = getFunctionInfo(function);

    assert(isValidFunction(function));
    assert(count >= IntHashMapSize(locals));

    IVSetSize(&localNames, offset + count);
    info->localCount = count;
    info->localNamesOffset = offset;
    IntHashMapIteratorInit(locals, &iter);
    while (IntHashMapIteratorNext(&iter, &name, &index))
    {
        IVSet(&localNames, offset + index - 1, name);
    }
}
