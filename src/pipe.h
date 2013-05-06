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

extern nonnull int PipeInit(Pipe *p);
extern nonnull void PipeDispose(Pipe *p);

extern nonnull void PipeAddListener(Pipe *p, PipeListener *listener);
extern nonnull void PipeConsume2(Pipe *restrict p1, Pipe *restrict p2);
