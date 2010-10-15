#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "common.h"
#include "file.h"
#include "log.h"
#include "vm.h"

#define MIN_READ_BUFFER 1024


typedef struct
{
    size_t begin;
    boolean echo;
} Buffer;

typedef struct
{
    bytevector buffer;
    bytevector bufferStack;
    int fd;
} Pipe;

static Pipe out;
static Pipe err;


static boolean buffered(Pipe *p)
{
    return ByteVectorSize(&p->bufferStack) != 0;
}

static void flush(Pipe *p, size_t size)
{
    size_t keep;

    if (buffered(p))
    {
        return;
    }
    keep = ByteVectorSize(&p->buffer) - size;
    /* TODO: Error handling */
    write(p->fd, ByteVectorGetPointer(&p->buffer, 0), size);
    ByteVectorMove(&p->buffer, size, 0, keep);
    ByteVectorSetSize(&p->buffer, keep);
}

static void autoflush(Pipe *p, size_t newData)
{
    size_t i;
    const byte *data;

    if (buffered(p))
    {
        return;
    }
    data = ByteVectorGetPointer(&p->buffer, ByteVectorSize(&p->buffer));
    for (i = newData; i; i--)
    {
        data--;
        if (*data == '\n')
        {
            flush(p, ByteVectorSize(&p->buffer) - newData + i);
            return;
        }
    }
}


ErrorCode LogInit(void)
{
    if (ByteVectorInit(&out.buffer, MIN_READ_BUFFER * 2) ||
        ByteVectorInit(&err.buffer, MIN_READ_BUFFER * 2) ||
        ByteVectorInit(&out.bufferStack, sizeof(Buffer) * 2) ||
        ByteVectorInit(&err.bufferStack, sizeof(Buffer) * 2))
    {
        return OUT_OF_MEMORY;
    }
    out.fd = STDOUT_FILENO;
    err.fd = STDERR_FILENO;
    return NO_ERROR;
}

void LogDispose(void)
{
    ByteVectorDispose(&out.buffer);
    ByteVectorDispose(&err.buffer);
    ByteVectorDispose(&out.bufferStack);
    ByteVectorDispose(&err.bufferStack);
}


void LogParseError(fileref file, size_t line, const char *message)
{
    /* TODO: Don't truncate line number. */
    printf("%s:%d: %s\n", FileGetName(file), (uint)line, message);
}

ErrorCode LogPrint(const char *text, size_t length)
{
    ErrorCode error;

    if (!length)
    {
        return LogNewline();
    }
    if (!buffered(&out) && !ByteVectorSize(&out.buffer) &&
        text[length - 1] == '\n')
    {
        /* TODO: Error handling */
        write(STDOUT_FILENO, text, length);
        return NO_ERROR;
    }
    error = ByteVectorAddData(&out.buffer, (const byte*)text, length);
    if (error)
    {
        return error;
    }
    autoflush(&out, length);
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
    if (!buffered(&out) && !ByteVectorSize(&out.buffer) &&
        text[length - 1] == '\n')
    {
        /* TODO: Error handling */
        write(STDOUT_FILENO, text, length);
        return NO_ERROR;
    }
    error = ByteVectorAddData(&out.buffer, (const byte*)text, length);
    if (error)
    {
        return error;
    }
    if (text[length - 1] == '\n')
    {
        flush(&out, ByteVectorSize(&out.buffer));
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
    p = ByteVectorGetAppendPointer(&out.buffer);
    error = ByteVectorGrow(&out.buffer, length + 1);
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
        ByteVectorPop(&out.buffer);
    }
    flush(&out, ByteVectorSize(&out.buffer));
    return NO_ERROR;
}

ErrorCode LogNewline(void)
{
    ErrorCode error = ByteVectorAdd(&out.buffer, '\n');
    if (error)
    {
        return error;
    }
    flush(&out, ByteVectorSize(&out.buffer));
    return NO_ERROR;
}

ErrorCode LogAutoNewline(void)
{
    if (ByteVectorSize(&out.buffer) && ByteVectorPeek(&out.buffer) != '\n')
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
            ByteVectorReserveAppendSize(&out.buffer, MIN_READ_BUFFER);
            ssize = read(fdOut, ByteVectorGetAppendPointer(&out.buffer),
                         ByteVectorGetReservedAppendSize(&out.buffer));
            if (ssize)
            {
                if (ssize > 0)
                {
                    ByteVectorGrow(&out.buffer, (size_t)ssize);
                    autoflush(&out, (size_t)ssize);
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
            ByteVectorReserveAppendSize(&err.buffer, MIN_READ_BUFFER);
            ssize = read(fdErr, ByteVectorGetAppendPointer(&err.buffer),
                         ByteVectorGetReservedAppendSize(&err.buffer));
            if (ssize)
            {
                if (ssize > 0)
                {
                    ByteVectorGrow(&err.buffer, (size_t)ssize);
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
    autoflush(&err, ByteVectorSize(&err.buffer));
    return NO_ERROR;
}

static ErrorCode pushBuffer(Pipe *p, boolean echo)
{
    size_t oldSize = ByteVectorSize(&p->bufferStack);
    ErrorCode error;
    Buffer *buffer;

    error = ByteVectorSetSize(&p->bufferStack, oldSize + sizeof(Buffer));
    if (error)
    {
        return error;
    }
    buffer = (Buffer*)ByteVectorGetPointer(&p->bufferStack, oldSize);
    buffer->begin = ByteVectorSize(&p->buffer);
    buffer->echo = echo;
    return NO_ERROR;
}

ErrorCode LogPushOutBuffer(boolean echo)
{
    return pushBuffer(&out, echo);
}

ErrorCode LogPushErrBuffer(boolean echo)
{
    return pushBuffer(&err, echo);
}

static void getBuffer(Pipe *p, const byte **output, size_t *length)
{
    Buffer *buffer = (Buffer*)ByteVectorGetPointer(
        &p->bufferStack, ByteVectorSize(&p->bufferStack) - sizeof(Buffer));

    *output = ByteVectorGetPointer(&p->buffer, buffer->begin);
    *length = ByteVectorSize(&p->buffer) - buffer->begin;
}

void LogGetOutBuffer(const byte **output, size_t *length)
{
    getBuffer(&out, output, length);
}

void LogGetErrBuffer(const byte **output, size_t *length)
{
    getBuffer(&err, output, length);
}

static void popBuffer(Pipe *p)
{
    size_t bufferOffset = ByteVectorSize(&p->bufferStack) - sizeof(Buffer);
    Buffer *buffer = (Buffer*)ByteVectorGetPointer(&p->bufferStack,
                                                   bufferOffset);

    if (!buffer->echo)
    {
        ByteVectorSetSize(&p->buffer, buffer->begin);
        ByteVectorSetSize(&p->bufferStack, bufferOffset);
    }
    else
    {
        ByteVectorSetSize(&p->bufferStack, bufferOffset);
        autoflush(p, ByteVectorSize(&p->buffer) - buffer->begin);
    }
}

void LogPopOutBuffer(void)
{
    popBuffer(&out);
}

void LogPopErrBuffer(void)
{
    popBuffer(&err);
}
