#include "common.h"
#include "util.h"

size_t UtilCountNewlines(const char *text, size_t size)
{
    size_t newlines = 0;
    while (size--)
    {
        if (*text++ == '\n')
        {
            newlines++;
        }
    }
    return newlines;
}
