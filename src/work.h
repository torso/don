struct _Work;

typedef bool (*WorkFunction)(struct _Work*, vref*);

typedef struct _Work
{
    WorkFunction function;
    VM *vm;
    const int *ip;
    vref condition;
    vref accessedFiles;
    vref modifiedFiles;
    uint argumentCount;
} Work;

extern void WorkInit(void);
extern void WorkDispose(void);

extern nonnull Work *WorkAdd(WorkFunction function, VM *vm, uint argumentCount, vref **arguments);
extern nonnull void WorkCommit(Work *work);
extern nonnull void WorkAbort(Work *work);
extern nonnull void WorkDiscard(const VM *vm);
extern bool WorkQueueEmpty(void);
extern bool WorkExecute(void);
