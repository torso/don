#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "fail.h"
#include "pipe.h"

#define MIN_READ_BUFFER 1024


int PipeInit(Pipe *p)
{
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
#if !HAVE_PIPE2
    fcntl(fd[0], FD_CLOEXEC);
    fcntl(fd[1], FD_CLOEXEC);
#endif
    BVInit(&p->buffer, MIN_READ_BUFFER);
    p->listener = null;
    p->fd = fd[0];
    return fd[1];
}

void PipeDispose(Pipe *p)
{
    if (p->fd)
    {
        close(p->fd);
    }
    BVDispose(&p->buffer);
}


void PipeAddListener(Pipe *p, PipeListener *listener)
{
    assert(p);
    assert(listener);
    assert(!listener->next);
    listener->next = p->listener;
    p->listener = listener;
    if (listener->output && BVSize(&p->buffer))
    {
        listener->output(BVGetPointer(&p->buffer, 0), BVSize(&p->buffer));
    }
}

static void pipeConsume(Pipe *p1, Pipe *p2, fd_set *set)
{
    PipeListener *listener;
    byte *data;
    ssize_t ssize;

    if (p1->fd && FD_ISSET(p1->fd, set))
    {
        data = BVGetAppendPointer(&p1->buffer, MIN_READ_BUFFER);
        ssize = read(p1->fd, data, MIN_READ_BUFFER + BVGetReservedAppendSize(&p1->buffer));
        if (ssize > 0)
        {
            BVSetSize(&p1->buffer, BVSize(&p1->buffer) - MIN_READ_BUFFER + (size_t)ssize);
            for (listener = p1->listener; listener; listener = listener->next)
            {
                if (listener->output)
                {
                    listener->output(data, (size_t)ssize);
                }
            }
        }
        else if (!ssize)
        {
            BVSetSize(&p1->buffer, BVSize(&p1->buffer) - MIN_READ_BUFFER);
            close(p1->fd);
            p1->fd = 0;
        }
        else if (errno != EWOULDBLOCK)
        {
            BVSetSize(&p1->buffer, BVSize(&p1->buffer) - MIN_READ_BUFFER);
            close(p1->fd);
            if (p2->fd)
            {
                close(p2->fd);
            }
            FailErrno(false);
        }
    }
}

void PipeConsume2(Pipe *p1, Pipe *p2)
{
    fd_set set;
    int status;

    assert(p1);
    assert(p2);
    assert(p1->fd);
    assert(p2->fd);

    while (p1->fd || p2->fd)
    {
        FD_ZERO(&set);
        if (p1->fd)
        {
            FD_SET(p1->fd, &set);
        }
        if (p2->fd)
        {
            FD_SET(p2->fd, &set);
        }
        status = select(FD_SETSIZE, &set, null, null, null);
        if (status < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            FailErrno(false);
        }

        pipeConsume(p1, p2, &set);
        pipeConsume(p2, p1, &set);
    }
}
