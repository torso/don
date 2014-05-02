#include "common.h"
#include <string.h>
#include "intvector.h"

#define VECTOR_NAME intvector
#define VECTOR_TYPE int
#define VECTOR_FUNC(name) IV##name
#include "vector.inc"

void IVGrowValue(intvector *v, int value, size_t size)
{
    int *write;
    int *stop;
    vectorGrow(v, size);
    write = IVGetWritePointer(v, IVSize(v));
    stop = write - size;
    while (write != stop)
    {
        *--write = value;
    }
}

void IVAppendString(intvector *v, const char *string, size_t length)
{
    size_t intLength = (length + 4) >> 2;
    int *write = IVGetAppendPointer(v, intLength + 1);
    *write++ = (int)length;
    write[intLength - 1] = 0;
    memcpy(write, string, length);
}
