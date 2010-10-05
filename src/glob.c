#include <memory.h>
#include "builder.h"
#include "glob.h"

boolean GlobMatch(const char *pattern, const char *string)
{
    for (;;)
    {
        switch (*pattern)
        {
        case '*':
            assert(!strchr(pattern + 1, '*')); /* TODO: Multiple globs */
            assert(!strchr(pattern + 1, '/')); /* TODO: Path glob */
            return !strchr(string, '/') &&
                !strcmp(pattern + 1, string + strlen(string) - strlen(pattern + 1));

        case 0:
            return !*string;
        }
        if (*pattern != *string)
        {
            return false;
        }
        pattern++;
        string++;
    }
}
