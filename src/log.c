#include "config.h"
#include <stdarg.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "fail.h"
#include "log.h"
#include "pipe.h"
#include "value.h"

#define MIN_READ_BUFFER 1024


typedef struct
{
    bytevector buffer;
    int fd;
    const char *prefix;
    size_t prefixLength;
} LogPipe;

static void logNewline(void);

static LogPipe out;


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
    bool lastWasNewline;

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
    out.fd = STDOUT_FILENO;
}

void LogDispose(void)
{
    BVDispose(&out.buffer);
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

void LogPrint(const char *data, size_t length)
{
    logPrint(&out, (const char*)data, length);
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

static void logPrintObjectAutoNewline(LogPipe *p, vref object)
{
    size_t length = VStringLength(object);
    byte *data;

    if (!length)
    {
        logPrint(p, "\n", 1);
        return;
    }
    data = BVGetAppendPointer(&p->buffer, length + 1);
    VWriteString(object, (char*)data);
    if (data[length - 1] != '\n')
    {
        data[length] = '\n';
    }
    else
    {
        BVSetSize(&p->buffer, BVSize(&p->buffer) - 1);
    }
    processNewData(p, BVSize(&p->buffer));
}

void LogPrintObjectAutoNewline(vref object)
{
    logPrintObjectAutoNewline(&out, object);
}

void logNewline(void)
{
    logPrint(&out, "\n", 1);
}

void LogAutoNewline(void)
{
    if (BVSize(&out.buffer) && BVPeek(&out.buffer) != '\n')
    {
        logNewline();
    }
}

void LogSetPrefix(const char *prefix, size_t length)
{
    out.prefix = prefix;
    out.prefixLength = length;
}
