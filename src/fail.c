#include "common.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "fail.h"

void Fail(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    cleanShutdown(EXIT_FAILURE);
}

void FailErrno(bool forked)
{
    fprintf(stderr, "don: %s\n", strerror(errno));
    if (forked)
    {
        _exit(EXIT_FAILURE);
    }
    cleanShutdown(EXIT_FAILURE);
}

void FailOOM(void)
{
    fputs("don: Out of memory\n", stderr);
    exit(EXIT_FAILURE);
}

void FailIO(const char *message, const char *filename)
{
    FailIOErrno(message, filename, errno);
}

void FailIOErrno(const char *message, const char *filename, int error)
{
    fprintf(stderr, "don: %s %s: %s\n", message, filename, strerror(error));
    cleanShutdown(EXIT_FAILURE);
}
