#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "fail.h"
#include "pipe.h"
#include "value.h"

#define MIN_READ_BUFFER 1024

typedef enum
{
    PIPE_UNUSED = 0,
    PIPE_READ,
    PIPE_WRITE
} PipeState;

typedef struct
{
    bytevector buffer;
    size_t bufferPos;
    int fd, fdSourceOrSink;
    PipeState state;
} Pipe;

static bytevector pipes;


static Pipe *getPipe(int handle)
{
    return (Pipe*)BVGetPointer(&pipes, (size_t)handle);
}

static void pipeDispose(Pipe *pipe, vref *value)
{
    pipe->state = PIPE_UNUSED;
    if (pipe->fd >= 0)
    {
        close(pipe->fd);
        pipe->fd = -1;
    }
    if (BVIsInitialized(&pipe->buffer))
    {
        if (value)
        {
            *value = VCreateString((const char*)BVGetPointer(&pipe->buffer, 0),
                                   BVSize(&pipe->buffer));
        }
        BVDispose(&pipe->buffer);
    }
    else if (value)
    {
        *value = VEmptyString;
    }
}

void PipeInit(void)
{
    /* TODO: Check this number. Use number of allowed concurrent jobs? */
    BVInit(&pipes, 16 * sizeof(Pipe));
}

void PipeDisposeAll(void)
{
    Pipe *pipe = (Pipe*)BVGetPointer(&pipes, 0);
    Pipe *stop = (Pipe*)((byte*)pipe + BVSize(&pipes));
    while (pipe < stop)
    {
        pipeDispose(pipe++, null);
    }
    BVDispose(&pipes);
}

void PipeProcess(void)
{
    fd_set readSet, writeSet;
    int status;
    Pipe *pipe = (Pipe*)BVGetPointer(&pipes, 0);
    Pipe *stop = (Pipe*)((byte*)pipe + BVSize(&pipes));
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    while (pipe < stop)
    {
        if (pipe->fd >= 0)
        {
            FD_SET(pipe->fd, pipe->state == PIPE_WRITE ? &writeSet : &readSet);
        }
        pipe++;
    }

wait:
    status = select(FD_SETSIZE, &readSet, &writeSet, null, null);
    if (unlikely(status < 0))
    {
        if (errno == EINTR)
        {
            goto wait;
        }
        FailErrno(false);
    }

    for (pipe = (Pipe*)BVGetPointer(&pipes, 0); pipe < stop; pipe++)
    {
        if (pipe->fd < 0)
        {
            continue;
        }

        if (pipe->state == PIPE_WRITE)
        {
            const byte *data;
            size_t left;

            assert(pipe->fdSourceOrSink < 0); /* TODO */
            if (!FD_ISSET(pipe->fd, &writeSet))
            {
                continue;
            }

            data = BVGetPointer(&pipe->buffer, pipe->bufferPos);
            left = BVSize(&pipe->buffer) - pipe->bufferPos;
            if (left)
            {
                ssize_t writeSize;
        writeAgain:
                writeSize = write(pipe->fd, data, left);
                if (writeSize < 0)
                {
                    if (errno == EINTR)
                    {
                        goto writeAgain;
                    }
                }
                pipe->bufferPos += (size_t)writeSize;
                left -= (size_t)writeSize;
            }
            if (!left)
            {
                close(pipe->fd);
                pipe->fd = -1;
            }
        }
        else if (FD_ISSET(pipe->fd, &readSet))
        {
            size_t oldSize;
            bool first = true;

            if (!BVIsInitialized(&pipe->buffer))
            {
                byte buffer[MIN_READ_BUFFER];
                ssize_t readSize;
                oldSize = 0;
                first = false;
        readAgain1:
                readSize = read(pipe->fd, buffer, sizeof(buffer));
                if (readSize > 0)
                {
                    BVInit(&pipe->buffer, MIN_READ_BUFFER + (size_t)readSize);
                    BVAddData(&pipe->buffer, buffer, (size_t)readSize);
                    if (readSize != sizeof(buffer))
                    {
                        goto readComplete;
                    }
                }
                else if (unlikely(readSize < 0))
                {
                    if (errno == EINTR)
                    {
                        goto readAgain1;
                    }
                    FailErrno(false);
                }
                else
                {
                    close(pipe->fd);
                    pipe->fd = -1;
                    continue;
                }
            }
            else
            {
                oldSize = BVSize(&pipe->buffer);
            }

            for (;;)
            {
                size_t prevSize = BVSize(&pipe->buffer);
                byte *pbuffer = BVGetAppendPointer(&pipe->buffer, MIN_READ_BUFFER);
                ssize_t readSize;
                size_t requestedSize;
        readAgain2:
                requestedSize = MIN_READ_BUFFER + BVGetReservedAppendSize(&pipe->buffer);
                readSize = read(pipe->fd, pbuffer, requestedSize);
                if (readSize > 0)
                {
                    first = false;
                    BVSetSize(&pipe->buffer, prevSize + (size_t)readSize);
                    if (requestedSize != (size_t)readSize)
                    {
                        break;
                    }
                }
                else if (readSize < 0)
                {
                    if (likely(errno == EWOULDBLOCK))
                    {
                        BVSetSize(&pipe->buffer, prevSize);
                        break;
                    }
                    if (errno == EINTR)
                    {
                        goto readAgain2;
                    }
                    FailErrno(false);
                }
                else
                {
                    BVSetSize(&pipe->buffer, prevSize);
                    if (first)
                    {
                        close(pipe->fd);
                        pipe->fd = -1;
                    }
                    break;
                }
            }

    readComplete:
            if (pipe->fdSourceOrSink >= 0)
            {
                size_t readSize = BVSize(&pipe->buffer) - oldSize;
                const byte *pbuffer = BVGetPointer(&pipe->buffer, oldSize);
                while (readSize)
                {
                    ssize_t writeSize = write(pipe->fdSourceOrSink, pbuffer, readSize);
                    if (unlikely(writeSize < 0))
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        FailErrno(false);
                    }
                    pbuffer += writeSize;
                    readSize -= (size_t)writeSize;
                }
            }
        }
    }
}


static Pipe *pipeCreate(int *pfd, bool read)
{
    int fd[2];
    int status;
    Pipe *pipe = (Pipe*)BVGetPointer(&pipes, 0);
    Pipe *stop = (Pipe*)((byte*)pipe + BVSize(&pipes));
    for (;; pipe++)
    {
        if (pipe == stop)
        {
            pipe = (Pipe*)BVGetAppendPointer(&pipes, sizeof(*pipe));
            break;
        }
        if (pipe->state == PIPE_UNUSED)
        {
            break;
        }
    }
#if HAVE_PIPE2
    status = pipe2(fd, O_CLOEXEC);
#else
    status = pipe(fd);
#endif
    if (unlikely(status))
    {
        FailErrno(false);
    }
    memset(pipe, 0, sizeof(*pipe));
#if !HAVE_PIPE2
    fcntl(fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, FD_CLOEXEC);
#endif
    pipe->fd = fd[read ? 1 : 0];
    fcntl(pipe->fd, F_SETFL, O_NONBLOCK);
    pipe->fdSourceOrSink = -1;
    *pfd = fd[read ? 0 : 1];
    return pipe;
}

int PipeCreateWrite(int *fdWrite)
{
    Pipe *pipe = pipeCreate(fdWrite, false);
    pipe->state = PIPE_READ;
    return (int)((byte*)pipe - BVGetPointer(&pipes, 0));
}

int PipeCreateRead(int *fdRead, bytevector **buffer, size_t bufferSize)
{
    Pipe *pipe = pipeCreate(fdRead, true);
    BVInit(&pipe->buffer, bufferSize);
    pipe->state = PIPE_WRITE;
    *buffer = &pipe->buffer;
    return (int)((byte*)pipe - BVGetPointer(&pipes, 0));
}

bool PipeIsOpen(int handle)
{
    Pipe *pipe = getPipe(handle);
    return pipe->fd >= 0;
}

void PipeDispose(int handle, vref *value)
{
    pipeDispose(getPipe(handle), value);
}

void PipeConnect(int handle, int fd)
{
    Pipe *pipe = getPipe(handle);
    assert(pipe->fdSourceOrSink < 0);
    assert(!BVIsInitialized(&pipe->buffer)); /* TODO: Copy buffered data */
    pipe->fdSourceOrSink = fd;
}
