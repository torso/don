#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "pipe.h"
#include "task.h"

#define MIN_READ_BUFFER 1024


void PipeInitFD(Pipe *p, int fd)
{
    BVInit(&p->buffer, 256);
    p->listener = null;
    p->fd = fd;
}

void PipeDispose(Pipe *p)
{
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
        BVReserveAppendSize(&p1->buffer, MIN_READ_BUFFER);
        data = BVGetAppendPointer(&p1->buffer);
        ssize = read(p1->fd, data, BVGetReservedAppendSize(&p1->buffer));
        if (ssize > 0)
        {
            BVGrow(&p1->buffer, (size_t)ssize);
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
            close(p1->fd);
            p1->fd = 0;
        }
        else if (errno != EWOULDBLOCK)
        {
            close(p1->fd);
            if (p2->fd)
            {
                close(p2->fd);
            }
            TaskFailErrno(false);
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
            TaskFailErrno(false);
        }

        pipeConsume(p1, p2, &set);
        pipeConsume(p2, p1, &set);
    }
}
