#include <string.h>
#include "common.h"
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
