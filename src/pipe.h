#define PIPE_H

typedef struct _PipeListener PipeListener;

struct _PipeListener
{
    PipeListener *next;
    void (*output)(const byte*, size_t);
};

typedef struct
{
    bytevector buffer;
    PipeListener *listener;
    int fd;
} Pipe;

nonnull int PipeInit(Pipe *p);
nonnull void PipeDispose(Pipe *p);

nonnull void PipeAddListener(Pipe *p, PipeListener *listener);
nonnull void PipeConsume2(Pipe *restrict p1, Pipe *restrict p2);
