#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include "common.h"
#include "task.h"

void TaskFail(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}

void TaskFailErrno(boolean forked)
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

void TaskFailOOM(void)
{
    fprintf(stderr, "Out of memory\n");
    exit(1);
}

void TaskFailIO(const char *filename)
{
    TaskFailIOErrno(errno, filename);
}

void TaskFailIOErrno(int error, const char *filename)
{
    switch (error)
    {
    case ENOENT:
        fprintf(stderr, "No such file or directory: %s\n", filename);
        break;

    case EISDIR:
        fprintf(stderr, "Expected file, but found directory: %s\n", filename);
        break;

    default:
        fprintf(stderr, "IO Error %d: %s\n", error, filename);
        break;
    }
    assert(false);
    exit(1);
}

void TaskFailVM(VM *vm unused)
{
    /* TODO: Print stack trace. */
    exit(1);
}
