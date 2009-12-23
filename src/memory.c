#include <stdlib.h>
#include <string.h>
#include "builder.h"

void *zmalloc(size_t size)
{
    void *data = malloc(size);
    assert(data); /* TODO: handle oom */
    if (data)
    {
        memset(data, 0, size);
    }
    return data;
}
