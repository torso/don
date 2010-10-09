#include "common.h"
#include "math.h"

boolean addOverflow(int a, int b)
{
    return (boolean)(b < 1 ? INT_MIN - b > a : INT_MAX - b < a);
}

boolean subOverflow(int a, int b)
{
    return (boolean)(b < 1 ? INT_MAX + b < a : INT_MIN + b > a);
}

uint roundToPow2(uint value)
{
    uint i = 1;
    assert(value <= (UINT_MAX >> 1) + 1);
    while (i < value)
    {
        i <<= 1;
    }
    return i;
}

size_t roundSizeToPow2(size_t value)
{
    size_t i = 1;
    assert(value <= (SIZE_MAX >> 1) + 1);
    while (i < value)
    {
        i <<= 1;
    }
    return i;
}
