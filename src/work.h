struct _Work;

typedef vref (*WorkFunction)(struct _Work*, vref*);

typedef struct _Work
{
    WorkFunction function;
    VM *vm;
    vref accessedFiles;
    vref modifiedFiles;
    uint argumentCount;
    int storeAt;
} Work;

nonnull Work *WorkAdd(WorkFunction function, VM *vm, const vref *arguments, uint argumentCount,
                      vref accessedFiles, vref modifiedFiles);
nonnull void WorkDiscard(Work *work);
void WorkExecute(Work *work);
