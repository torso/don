#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "common.h"
#include "vm.h"
#include "fileindex.h"
#include "log.h"
#include "stringpool.h"

#define MIN_READ_BUFFER 1024


static bytevector out;
static bytevector err;
static intvector buffer;


static boolean buffered(void)
{
    return IntVectorSize(&buffer) != 0;
}

static void flush(bytevector *v, int fd, size_t size)
{
    size_t keep;

    if (buffered())
    {
        return;
    }
    keep = ByteVectorSize(v) - size;
    /* TODO: Error handling */
    write(fd, ByteVectorGetPointer(v, 0), size);
    ByteVectorMove(v, size, 0, keep);
    ByteVectorSetSize(v, keep);
}

static void flushOut(size_t newData)
{
    flush(&out, STDOUT_FILENO, newData);
}

static void autoflush(bytevector *v, int fd, size_t newData)
{
    size_t i;
    const byte *data;

    if (buffered())
    {
        return;
    }
    data = ByteVectorGetPointer(v, ByteVectorSize(v));
    for (i = newData; i; i--)
    {
        data--;
        if (*data == '\n')
        {
            flush(v, fd, ByteVectorSize(v) - newData + i);
            return;
        }
    }
}

static void autoflushOut(size_t newData)
{
    autoflush(&out, STDOUT_FILENO, newData);
}

static void autoflushErr(size_t newData)
{
    autoflush(&err, STDERR_FILENO, newData);
}


ErrorCode LogInit(void)
{
    ErrorCode error = ByteVectorInit(&out, MIN_READ_BUFFER * 2);
    if (error)
    {
        return error;
    }
    error = ByteVectorInit(&err, MIN_READ_BUFFER * 2);
    if (error)
    {
        return error;
    }
    return IntVectorInit(&buffer);
}

void LogDispose(void)
{
    ByteVectorDispose(&out);
    ByteVectorDispose(&err);
    IntVectorDispose(&buffer);
}


void LogParseError(fileref file, uint line, const char *message)
{
    printf("%s:%d: %s\n", FileIndexGetName(file), line, message);
}

ErrorCode LogPrint(const char *text, size_t length)
{
    ErrorCode error;

    if (!length)
    {
        return LogNewline();
    }
    if (!buffered() && !ByteVectorSize(&out) && text[length - 1] == '\n')
    {
        /* TODO: Error handling */
        write(STDOUT_FILENO, text, length);
        return NO_ERROR;
    }
    error = ByteVectorAddData(&out, (const byte*)text, length);
    if (error)
    {
        return error;
    }
    autoflushOut(length);
    return NO_ERROR;
}

ErrorCode LogPrintSZ(const char *text)
{
    return LogPrint(text, strlen(text));
}

ErrorCode LogPrintAutoNewline(const char *text, size_t length)
{
    ErrorCode error;

    if (!length)
    {
        return LogNewline();
    }
    if (!buffered() && !ByteVectorSize(&out) && text[length - 1] == '\n')
    {
        /* TODO: Error handling */
        write(STDOUT_FILENO, text, length);
        return NO_ERROR;
    }
    error = ByteVectorAddData(&out, (const byte*)text, length);
    if (error)
    {
        return error;
    }
    if (text[length - 1] == '\n')
    {
        flushOut(ByteVectorSize(&out));
        return NO_ERROR;
    }
    return LogNewline();
}

ErrorCode LogPrintObjectAutoNewline(VM *vm, objectref object)
{
    size_t length = HeapStringLength(vm, object);
    byte *p;
    ErrorCode error;

    if (!length)
    {
        return LogNewline();
    }
    p = ByteVectorGetAppendPointer(&out);
    error = ByteVectorGrow(&out, length + 1);
    if (error)
    {
        return error;
    }
    HeapWriteString(vm, object, (char*)p);
    if (p[length - 1] != '\n')
    {
        p[length] = '\n';
    }
    else
    {
        ByteVectorPop(&out);
    }
    flushOut(ByteVectorSize(&out));
    return NO_ERROR;
}

ErrorCode LogNewline(void)
{
    ErrorCode error = ByteVectorAdd(&out, '\n');
    if (error)
    {
        return error;
    }
    flushOut(ByteVectorSize(&out));
    return NO_ERROR;
}

ErrorCode LogAutoNewline(void)
{
    if (ByteVectorSize(&out) && ByteVectorPeek(&out) != '\n')
    {
        return LogNewline();
    }
    return NO_ERROR;
}

ErrorCode LogConsumePipes(int fdOut, int fdErr)
{
    ssize_t ssize;
    int status;

    status = fcntl(fdOut, F_GETFL, 0);
    fcntl(fdOut, F_SETFL, status | O_NONBLOCK);
    status = fcntl(fdErr, F_GETFL, 0);
    fcntl(fdErr, F_SETFL, status | O_NONBLOCK);

    /* TODO: Sleep when no data is available. */
    while (fdOut || fdErr)
    {
        if (fdOut)
        {
            ByteVectorReserveAppendSize(&out, MIN_READ_BUFFER);
            ssize = read(fdOut, ByteVectorGetAppendPointer(&out),
                         ByteVectorGetReservedAppendSize(&out));
            if (ssize)
            {
                if (ssize > 0)
                {
                    ByteVectorGrow(&out, (size_t)ssize);
                    autoflushOut((size_t)ssize);
                }
                else if (errno != EWOULDBLOCK)
                {
                    close(fdOut);
                    close(fdErr);
                    /* TODO: Error handling */
                    return OUT_OF_MEMORY;
                }
            }
            else
            {
                close(fdOut);
                fdOut = 0;
            }
        }
        if (fdErr)
        {
            ByteVectorReserveAppendSize(&err, MIN_READ_BUFFER);
            ssize = read(fdErr, ByteVectorGetAppendPointer(&err),
                         ByteVectorGetReservedAppendSize(&err));
            if (ssize)
            {
                if (ssize > 0)
                {
                    ByteVectorGrow(&err, (size_t)ssize);
                }
                else if (errno != EWOULDBLOCK)
                {
                    close(fdOut);
                    close(fdErr);
                    /* TODO: Error handling */
                    return OUT_OF_MEMORY;
                }
            }
            else
            {
                close(fdErr);
                fdErr = 0;
            }
        }
    }
    autoflushErr(ByteVectorSize(&err));
    return NO_ERROR;
}

ErrorCode LogPushBuffer(void)
{
    ErrorCode error = IntVectorAdd(&buffer, (uint)ByteVectorSize(&out));
    if (error)
    {
        return error;
    }
    error = IntVectorAdd(&buffer, (uint)ByteVectorSize(&err));
    if (error)
    {
        IntVectorPop(&buffer);
    }
    return error;
}

void LogPopBuffer(VM *vm, objectref *stringOut, objectref *stringErr)
{
    uint errOffset = IntVectorPop(&buffer);
    uint outOffset = IntVectorPop(&buffer);
    size_t outSize = ByteVectorSize(&out);
    size_t errSize = ByteVectorSize(&err);

    if (!outSize)
    {
        *stringOut = vm->emptyString;
    }
    else
    {
        if (ByteVectorPeek(&out) == '\n')
        {
            ByteVectorPop(&out);
        }
        *stringOut = HeapCreateString(
            vm, (const char*)ByteVectorGetPointer(&out, outOffset),
            ByteVectorSize(&out) - outOffset);
        ByteVectorSetSize(&out, outOffset);
    }
    *stringErr = vm->emptyString;
    if (errSize && *stringOut)
    {
        if (ByteVectorPeek(&err) == '\n')
        {
            ByteVectorPop(&err);
        }
        if (*stringOut)
        {
            *stringErr = HeapCreateString(
                vm, (const char*)ByteVectorGetPointer(&err, errOffset),
                ByteVectorSize(&err) - errOffset);
        }
    }
    ByteVectorSetSize(&err, errOffset);
}
