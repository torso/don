#ifdef DEBUG

#include <stdio.h>
#include <signal.h>
#include "builder.h"

void _assert(const char *expression, const char *file, int line)
{
    printf("Assertion failed: %s:%d: %s\n", file, line, expression);
    raise(SIGABRT);
}

#endif
