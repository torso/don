struct _Work;

typedef bool (*WorkFunction)(struct _Work*, vref*);

typedef struct _Work
{
    WorkFunction function;
    VMBranch *branch;
    const int *ip;
    vref accessedFiles;
    vref modifiedFiles;
    uint argumentCount;
} Work;

void WorkInit(void);
void WorkDispose(void);

nonnull Work *WorkAdd(WorkFunction function, VM *vm, uint argumentCount, vref **arguments);
nonnull void WorkCommit(Work *work);
nonnull void WorkAbort(Work *work);
nonnull void WorkDiscard(const VMBranch *branch);
bool WorkQueueEmpty(void);
bool WorkExecute(void);
