#include "builder.h"
#include "math.h"

uint roundToPow2(uint value)
{
    uint i = 1;
    assert(value <= (MAX_UINT >> 1) + 1);
    while (i < value)
    {
        i <<= 1;
    }
    return i;
}
