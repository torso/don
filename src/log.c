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
    const char *prefix;
    size_t prefixLength;
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

static ErrorCode processNewData(Pipe *p, size_t newData)
{
    size_t beginOffset;
    size_t offset;
    boolean lastWasNewline;

    if (!newData)
    {
        return NO_ERROR;
    }
    if (!p->prefixLength)
    {
        autoflush(p, newData);
        return NO_ERROR;
    }
    beginOffset = ByteVectorSize(&p->buffer) - newData;
    offset = beginOffset;
    lastWasNewline = false;
    if (offset == 0)
    {
        lastWasNewline = true;
    }
    while (newData--)
    {
        if (lastWasNewline)
        {
            lastWasNewline = false;
            ByteVectorInsertData(&p->buffer, offset,
                                 (const byte*)p->prefix, p->prefixLength);
            offset += p->prefixLength;
        }
        if (ByteVectorGet(&p->buffer, offset) == '\n')
        {
            lastWasNewline = true;
        }
        offset++;
    }
    autoflush(p, ByteVectorSize(&p->buffer) - beginOffset);
    return NO_ERROR;
}


void LogInit(void)
{
    ByteVectorInit(&out.buffer, MIN_READ_BUFFER * 2);
    ByteVectorInit(&err.buffer, MIN_READ_BUFFER * 2);
    ByteVectorInit(&out.bufferStack, sizeof(Buffer) * 2);
    ByteVectorInit(&err.bufferStack, sizeof(Buffer) * 2);
    out.fd = STDOUT_FILENO;
    err.fd = STDERR_FILENO;
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
    ByteVectorAddData(&out.buffer, (const byte*)text, length);
    return processNewData(&out, length);
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
    error = LogPrint(text, length);
    if (error)
    {
        return error;
    }
    if (text[length - 1] != '\n')
    {
        return LogNewline();
    }
    return NO_ERROR;
}

ErrorCode LogPrintObjectAutoNewline(VM *vm, objectref object)
{
    size_t length = HeapStringLength(vm, object);
    byte *p;

    if (!length)
    {
        return LogNewline();
    }
    p = ByteVectorGetAppendPointer(&out.buffer);
    ByteVectorGrow(&out.buffer, length + 1);
    HeapWriteString(vm, object, (char*)p);
    if (p[length - 1] != '\n')
    {
        p[length] = '\n';
    }
    else
    {
        ByteVectorPop(&out.buffer);
    }
    return processNewData(&out, ByteVectorSize(&out.buffer));
}

ErrorCode LogNewline(void)
{
    return LogPrint("\n", 1);
}

ErrorCode LogAutoNewline(void)
{
    if (ByteVectorSize(&out.buffer) && ByteVectorPeek(&out.buffer) != '\n')
    {
        return LogNewline();
    }
    return NO_ERROR;
}

void LogSetPrefix(const char *prefix, size_t length)
{
    out.prefix = prefix;
    out.prefixLength = length;
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
                    processNewData(&out, (size_t)ssize);
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
    processNewData(&err, ByteVectorSize(&err.buffer));
    return NO_ERROR;
}

static void pushBuffer(Pipe *p, boolean echo)
{
    size_t oldSize = ByteVectorSize(&p->bufferStack);
    Buffer *buffer;

    ByteVectorSetSize(&p->bufferStack, oldSize + sizeof(Buffer));
    buffer = (Buffer*)ByteVectorGetPointer(&p->bufferStack, oldSize);
    buffer->begin = ByteVectorSize(&p->buffer);
    buffer->echo = echo;
}

void LogPushOutBuffer(boolean echo)
{
    pushBuffer(&out, echo);
}

void LogPushErrBuffer(boolean echo)
{
    pushBuffer(&err, echo);
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
