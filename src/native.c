#include <stdlib.h>
#include "builder.h"
#include "stringpool.h"
#include "native.h"

static stringref echoParameterNames[1];

nativefunctionref NativeFindFunction(stringref name)
{
    if (name == StringPoolAdd("echo"))
    {
        return 0;
    }
    return -1;
}

uint NativeGetParameterCount(nativefunctionref function)
{
    assert(function == 0);
    return 1;
}

uint NativeGetMinimumArgumentCount(nativefunctionref function)
{
    assert(function == 0);
    return 1;
}

stringref *NativeGetParameterNames(nativefunctionref function)
{
    assert(function == 0);
    return echoParameterNames;
}
