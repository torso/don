#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "file.h"
#include "instruction.h"
#include "heap.h"
#include "log.h"
#include "stringpool.h"
#include "task.h"

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
    int echoDisable;
    size_t flushed;
    const char *prefix;
    size_t prefixLength;
} Pipe;

static Pipe out;
static Pipe err;
static boolean hasParseError;


static boolean buffered(Pipe *p)
{
    return BVSize(&p->bufferStack) != 0;
}

static void logWrite(int filedes, const byte *buffer, size_t size)
{
    ssize_t written;

    while (size)
    {
        written = write(filedes, buffer, size);
        if (written < 0)
        {
            TaskFailErrno(false);
        }
        size -= (size_t)written;
        buffer += written;
    }
}

static void flush(Pipe *p, size_t size)
{
    size_t keep;

    if (p->echoDisable)
    {
        return;
    }
    keep = BVSize(&p->buffer) - size;
    logWrite(p->fd, BVGetPointer(&p->buffer, p->flushed), size - p->flushed);
    if (buffered(p))
    {
        p->flushed = size;
    }
    else
    {
        BVMove(&p->buffer, size, p->flushed, keep);
        BVSetSize(&p->buffer, p->flushed + keep);
    }
}

static void autoflush(Pipe *p, size_t newData)
{
    size_t i;
    const byte *data;

    if (p->echoDisable)
    {
        return;
    }
    data = BVGetPointer(&p->buffer, BVSize(&p->buffer));
    for (i = newData; i; i--)
    {
        data--;
        if (*data == '\n')
        {
            flush(p, BVSize(&p->buffer) - newData + i);
            return;
        }
    }
}

static void processNewData(Pipe *p, size_t newData)
{
    size_t beginOffset;
    size_t offset;
    boolean lastWasNewline;

    if (!newData)
    {
        return;
    }
    if (!p->prefixLength)
    {
        autoflush(p, newData);
        return;
    }
    beginOffset = BVSize(&p->buffer) - newData;
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
            BVInsertData(&p->buffer, offset,
                         (const byte*)p->prefix, p->prefixLength);
            offset += p->prefixLength;
        }
        if (BVGet(&p->buffer, offset) == '\n')
        {
            lastWasNewline = true;
        }
        offset++;
    }
    autoflush(p, BVSize(&p->buffer) - beginOffset);
}


void LogInit(void)
{
    BVInit(&out.buffer, MIN_READ_BUFFER * 2);
    BVInit(&err.buffer, MIN_READ_BUFFER * 2);
    BVInit(&out.bufferStack, sizeof(Buffer) * 2);
    BVInit(&err.bufferStack, sizeof(Buffer) * 2);
    out.fd = STDOUT_FILENO;
    err.fd = STDERR_FILENO;
}

void LogDispose(void)
{
    BVDispose(&out.buffer);
    BVDispose(&err.buffer);
    BVDispose(&out.bufferStack);
    BVDispose(&err.bufferStack);
}


boolean LogFlushParseErrors(void)
{
    return hasParseError;
}

void LogParseError(stringref filename, size_t line, const char *format, va_list ap)
{
    hasParseError = true;
    fprintf(stderr, "%s:%ld: ", StringPoolGetString(filename), line);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
}

static void logPrint(Pipe *p, const char *text, size_t length)
{
    if (!length)
    {
        text = "\n";
        length = 1;
    }
    if (!buffered(p) && !BVSize(&p->buffer) &&
        text[length - 1] == '\n')
    {
        logWrite(p->fd, (const byte*)text, length);
        return;
    }
    BVAddData(&p->buffer, (const byte*)text, length);
    processNewData(p, length);
}

void LogPrint(const char *text, size_t length)
{
    logPrint(&out, text, length);
}

void LogPrintErr(const char *text, size_t length)
{
    logPrint(&err, text, length);
}

void LogPrintSZ(const char *text)
{
    logPrint(&out, text, strlen(text));
}

void LogPrintErrSZ(const char *text)
{
    logPrint(&err, text, strlen(text));
}

void LogPrintAutoNewline(const char *text, size_t length)
{
    if (!length)
    {
        LogNewline();
        return;
    }
    LogPrint(text, length);
    if (text[length - 1] != '\n')
    {
        LogNewline();
    }
}

void LogPrintErrAutoNewline(const char *text, size_t length)
{
    if (!length)
    {
        LogErrNewline();
        return;
    }
    LogPrintErr(text, length);
    if (text[length - 1] != '\n')
    {
        LogErrNewline();
    }
}

static void logPrintObjectAutoNewline(Pipe *p, objectref object)
{
    size_t length = HeapStringLength(object);
    byte *data;

    if (!length)
    {
        logPrint(p, "\n", 1);
        return;
    }
    BVReserveAppendSize(&p->buffer, length + 1);
    data = BVGetAppendPointer(&p->buffer);
    HeapWriteString(object, (char*)data);
    if (data[length - 1] != '\n')
    {
        data[length] = '\n';
        BVGrow(&p->buffer, length + 1);
    }
    else
    {
        BVGrow(&p->buffer, length);
    }
    processNewData(p, BVSize(&p->buffer));
}

void LogPrintObjectAutoNewline(objectref object)
{
    logPrintObjectAutoNewline(&out, object);
}

void LogPrintErrObjectAutoNewline(objectref object)
{
    logPrintObjectAutoNewline(&err, object);
}

void LogNewline(void)
{
    logPrint(&out, "\n", 1);
}

void LogErrNewline(void)
{
    logPrint(&err, "\n", 1);
}

void LogAutoNewline(void)
{
    if (BVSize(&out.buffer) && BVPeek(&out.buffer) != '\n')
    {
        LogNewline();
    }
}

void LogErrAutoNewline(void)
{
    if (BVSize(&err.buffer) && BVPeek(&err.buffer) != '\n')
    {
        LogErrNewline();
    }
}

void LogSetPrefix(const char *prefix, size_t length)
{
    out.prefix = prefix;
    out.prefixLength = length;
}

void LogConsumePipes(int fdOut, int fdErr)
{
    fd_set set;
    ssize_t ssize;
    int status;
    struct timeval tv;

    memset(&tv, 0, sizeof(tv));
    while (fdOut || fdErr)
    {
        FD_ZERO(&set);
        if (fdOut)
        {
            FD_SET(fdOut, &set);
        }
        if (fdErr)
        {
            FD_SET(fdErr, &set);
        }
        status = select(FD_SETSIZE, &set, null, null,
                        !out.echoDisable &&
                        BVSize(&out.buffer) - out.flushed ?
                        &tv : null);
        if (status < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            TaskFailErrno(false);
        }
        if (status == 0)
        {
            flush(&out, BVSize(&out.buffer));
            continue;
        }

        if (FD_ISSET(fdOut, &set))
        {
            BVReserveAppendSize(&out.buffer, MIN_READ_BUFFER);
            ssize = read(fdOut, BVGetAppendPointer(&out.buffer),
                         BVGetReservedAppendSize(&out.buffer));
            if (ssize)
            {
                if (ssize > 0)
                {
                    BVGrow(&out.buffer, (size_t)ssize);
                    processNewData(&out, (size_t)ssize);
                }
                else if (errno != EWOULDBLOCK)
                {
                    close(fdOut);
                    close(fdErr);
                    TaskFailErrno(false);
                }
            }
            else
            {
                close(fdOut);
                fdOut = 0;
            }
        }
        if (FD_ISSET(fdErr, &set))
        {
            BVReserveAppendSize(&err.buffer, MIN_READ_BUFFER);
            ssize = read(fdErr, BVGetAppendPointer(&err.buffer),
                         BVGetReservedAppendSize(&err.buffer));
            if (ssize)
            {
                if (ssize > 0)
                {
                    BVGrow(&err.buffer, (size_t)ssize);
                }
                else if (errno != EWOULDBLOCK)
                {
                    close(fdOut);
                    close(fdErr);
                    TaskFailErrno(false);
                }
            }
            else
            {
                close(fdErr);
                fdErr = 0;
            }
        }
    }
    processNewData(&err, BVSize(&err.buffer));
}

static void pushBuffer(Pipe *p, boolean echo)
{
    size_t oldSize = BVSize(&p->bufferStack);
    Buffer *buffer;

    BVSetSize(&p->bufferStack, oldSize + sizeof(Buffer));
    buffer = (Buffer*)BVGetPointer(&p->bufferStack, oldSize);
    buffer->begin = BVSize(&p->buffer);
    buffer->echo = echo;
    if (!echo)
    {
        p->echoDisable++;
    }
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
    Buffer *buffer = (Buffer*)BVGetPointer(
        &p->bufferStack, BVSize(&p->bufferStack) - sizeof(Buffer));

    *output = BVGetPointer(&p->buffer, buffer->begin);
    *length = BVSize(&p->buffer) - buffer->begin;
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
    size_t bufferOffset = BVSize(&p->bufferStack) - sizeof(Buffer);
    Buffer *buffer = (Buffer*)BVGetPointer(&p->bufferStack,
                                           bufferOffset);

    if (!buffer->echo)
    {
        BVSetSize(&p->buffer, buffer->begin);
        BVSetSize(&p->bufferStack, bufferOffset);
        p->echoDisable--;
    }
    else
    {
        BVSetSize(&p->bufferStack, bufferOffset);
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
