#include <string.h>
#include "common.h"
#include "glob.h"

boolean GlobMatch(const char *pattern, size_t patternLength,
                  const char *string, size_t stringLength)
{
    while (patternLength--)
    {
        assert(*pattern);
        assert(*pattern != '/');
        switch (*pattern)
        {
        case '\\':
            assert(false); /* TODO: Escape sequences in globs. */
            break;

        case '*':
            pattern++;
            assert(!memchr(pattern, '*', patternLength)); /* TODO: Multiple globs */
            assert(!memchr(pattern, '\\', patternLength)); /* TODO: Escape sequences in globs */
            return stringLength >= patternLength &&
                !memcmp(pattern, string + stringLength - patternLength, patternLength);
        }
        if (*pattern != *string)
        {
            return false;
        }
        pattern++;
        string++;
        stringLength--;
    }
    return !*string;
}
