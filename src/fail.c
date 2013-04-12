#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "fail.h"

void Fail(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}

void FailErrno(boolean forked)
{
    switch (errno)
    {
    default:
        fprintf(stderr, "Error %d\n", errno);
        break;
    }
    if (forked)
    {
        _exit(1);
    }
    exit(1);
}

void FailOOM(void)
{
    fprintf(stderr, "Out of memory\n");
    exit(1);
}

void FailIO(const char *message, const char *filename)
{
    FailIOErrno(message, filename, errno);
}

void FailIOErrno(const char *message, const char *filename, int error)
{
    fprintf(stderr, "%s %s: %s\n", message, filename, strerror(error));
    exit(1);
}

void FailVM(VM *vm unused)
{
    /* TODO: Print stack trace. */
    exit(1);
}
