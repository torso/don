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

typedef struct
{
    bytevector buffer;
    int fdRead, fdWrite;
} Pipe;

static bytevector pipes;


static Pipe *getPipe(int fd)
{
    Pipe *pipe = (Pipe*)BVGetPointer(&pipes, 0);
    for (;;)
    {
        assert((size_t)((byte*)pipe - BVGetPointer(&pipes, 0)) < BVSize(&pipes));
        if (pipe->fdRead == fd)
        {
            return pipe;
        }
        pipe++;
    }
}

static void pipeDispose(Pipe *pipe, vref *value)
{
    close(pipe->fdRead);
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
    fd_set set;
    int status;
    Pipe *pipe = (Pipe*)BVGetPointer(&pipes, 0);
    Pipe *stop = (Pipe*)((byte*)pipe + BVSize(&pipes));
    FD_ZERO(&set);
    while (pipe < stop)
    {
        FD_SET(pipe->fdRead, &set);
        pipe++;
    }

wait:
    status = select(FD_SETSIZE, &set, null, null, null);
    if (status < 0)
    {
        if (errno == EINTR)
        {
            goto wait;
        }
        FailErrno(false);
    }

    pipe = (Pipe*)BVGetPointer(&pipes, 0);
    while (pipe < stop)
    {
        if (FD_ISSET(pipe->fdRead, &set))
        {
            size_t oldSize;

            if (!BVIsInitialized(&pipe->buffer))
            {
                byte buffer[MIN_READ_BUFFER];
                ssize_t readSize;
        readAgain1:
                readSize = read(pipe->fdRead, buffer, sizeof(buffer));
                if (readSize > 0)
                {
                    BVInit(&pipe->buffer, MIN_READ_BUFFER + (size_t)readSize);
                    BVAddData(&pipe->buffer, buffer, (size_t)readSize);
                }
                else if (readSize < 0)
                {
                    if (errno == EINTR)
                    {
                        goto readAgain1;
                    }
                    FailErrno(false);
                }
                else
                {
                    goto nextPipe;
                }
                oldSize = 0;
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
        readAgain2:
                readSize = read(pipe->fdRead, pbuffer,
                                MIN_READ_BUFFER + BVGetReservedAppendSize(&pipe->buffer));
                if (readSize > 0)
                {
                    BVSetSize(&pipe->buffer, prevSize + (size_t)readSize);
                }
                else if (readSize < 0)
                {
                    if (errno == EWOULDBLOCK)
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
                    break;
                }
            }

            if (pipe->fdWrite >= 0)
            {
                size_t readSize = BVSize(&pipe->buffer) - oldSize;
                const byte *pbuffer = BVGetPointer(&pipe->buffer, oldSize);
                while (readSize)
                {
                    ssize_t writeSize = write(pipe->fdWrite, pbuffer, readSize);
                    if (writeSize < 0)
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
nextPipe:
        pipe++;
    }
}


int PipeCreate(int *fdWrite)
{
    Pipe *pipe = (Pipe*)BVGetAppendPointer(&pipes, sizeof(*pipe));
    int fd[2];
#if HAVE_PIPE2
    int status = pipe2(fd, O_CLOEXEC);
#else
    int status = pipe(fd);
#endif
    if (status)
    {
        FailErrno(false);
    }
    memset(pipe, 0, sizeof(*pipe));
    fcntl(fd[0], F_SETFL, O_NONBLOCK);
#if !HAVE_PIPE2
    fcntl(fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, FD_CLOEXEC);
#endif
    pipe->fdRead = fd[0];
    pipe->fdWrite = -1;
    *fdWrite = fd[1];
    return fd[0];
}

void PipeDispose(int fd, vref *value)
{
    Pipe *pipe = getPipe(fd);
    size_t offset = (size_t)((byte*)pipe - BVGetPointer(&pipes, 0));
    pipeDispose(pipe, value);
    BVRemoveRange(&pipes, offset, sizeof(Pipe));
}

void PipeConnect(int fdFrom, int fdTo)
{
    Pipe *pipe = getPipe(fdFrom);
    assert(pipe->fdWrite < 0);
    assert(!BVIsInitialized(&pipe->buffer)); /* TODO: Copy buffered data */
    pipe->fdWrite = fdTo;
}
