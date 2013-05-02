#include <stdarg.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "fail.h"
#include "file.h"
#include "heap.h"
#include "pipe.h"
#include "log.h"
#include "stringpool.h"

#define MIN_READ_BUFFER 1024


typedef struct
{
    bytevector buffer;
    int fd;
    const char *prefix;
    size_t prefixLength;
} LogPipe;

static nonnull void logPrintRaw(const byte *data, size_t length);
static nonnull void logPrintErrRaw(const byte *data, size_t length);
static void logNewline(void);
static void logErrNewline(void);

static LogPipe out;
static LogPipe err;
static boolean hasParseError;

PipeListener LogPipeOutListener =
{
    null, logPrintRaw
};

PipeListener LogPipeErrListener =
{
    null, logPrintErrRaw
};


static void logWrite(int filedes, const byte *buffer, size_t size)
{
    ssize_t written;

    while (size)
    {
        written = write(filedes, buffer, size);
        if (written < 0)
        {
            FailErrno(false);
        }
        size -= (size_t)written;
        buffer += written;
    }
}

static void flush(LogPipe *p, size_t size)
{
    size_t keep;

    keep = BVSize(&p->buffer) - size;
    logWrite(p->fd, BVGetPointer(&p->buffer, 0), size);
    BVMove(&p->buffer, size, 0, keep);
    BVSetSize(&p->buffer, keep);
}

static void autoflush(LogPipe *p, size_t newData)
{
    size_t i;
    const byte *data;

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

static void processNewData(LogPipe *p, size_t newData)
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
    out.fd = STDOUT_FILENO;
    err.fd = STDERR_FILENO;
}

void LogDispose(void)
{
    BVDispose(&out.buffer);
    BVDispose(&err.buffer);
}


boolean LogFlushParseErrors(void)
{
    return hasParseError;
}

void LogParseError(vref filename, size_t line, const char *format, va_list ap)
{
    hasParseError = true;
    fprintf(stderr, "%s:%ld: ", HeapGetString(filename), line);
    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
}

static void logPrint(LogPipe *p, const char *text, size_t length)
{
    if (!length)
    {
        text = "\n";
        length = 1;
    }
    if (!BVSize(&p->buffer) && text[length - 1] == '\n')
    {
        logWrite(p->fd, (const byte*)text, length);
        return;
    }
    BVAddData(&p->buffer, (const byte*)text, length);
    processNewData(p, length);
}

void logPrintRaw(const byte *data, size_t length)
{
    logPrint(&out, (const char*)data, length);
}

void logPrintErrRaw(const byte *data, size_t length)
{
    logPrint(&err, (const char*)data, length);
}

void LogPrintAutoNewline(const char *text, size_t length)
{
    if (!length)
    {
        logNewline();
        return;
    }
    logPrint(&out, text, length);
    if (text[length - 1] != '\n')
    {
        logNewline();
    }
}

void LogPrintErrAutoNewline(const char *text, size_t length)
{
    if (!length)
    {
        logErrNewline();
        return;
    }
    logPrint(&err, text, length);
    if (text[length - 1] != '\n')
    {
        logErrNewline();
    }
}

static void logPrintObjectAutoNewline(LogPipe *p, vref object)
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

void LogPrintObjectAutoNewline(vref object)
{
    logPrintObjectAutoNewline(&out, object);
}

void LogPrintErrObjectAutoNewline(vref object)
{
    logPrintObjectAutoNewline(&err, object);
}

void logNewline(void)
{
    logPrint(&out, "\n", 1);
}

void logErrNewline(void)
{
    logPrint(&err, "\n", 1);
}

void LogAutoNewline(void)
{
    if (BVSize(&out.buffer) && BVPeek(&out.buffer) != '\n')
    {
        logNewline();
    }
}

void LogErrAutoNewline(void)
{
    if (BVSize(&err.buffer) && BVPeek(&err.buffer) != '\n')
    {
        logErrNewline();
    }
}

void LogSetPrefix(const char *prefix, size_t length)
{
    out.prefix = prefix;
    out.prefixLength = length;
}
