#include "config.h"
#include "common.h"
#include "std.h"

#if !HAVE_MEMRCHR
void *memrchr(const void *s, int c, size_t n)
{
    const byte *p = (const byte*)s;
    while (n)
    {
        if (p[--n] == c)
        {
            return (void*)(p + n);
        }
    }
    return null;
}
#endif
