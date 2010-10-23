#include <errno.h>
#include <stdio.h>
#include "common.h"
#include "task.h"

void TaskFailErrno(void)
{
    switch (errno)
    {
    default:
        printf("Error %d\n", errno);
        break;
    }
    abort();
}

void TaskFailOOM(void)
{
    printf("Out of memory\n");
    abort();
}

void TaskFailIO(const char *filename)
{
    switch (errno)
    {
    case ENOENT:
        printf("No such file or directory: %s\n", filename);
        break;

    default:
        printf("IO Error %d: %s\n", errno, filename);
        break;
    }
    abort();
}
