struct _Job;

typedef vref (*JobFunction)(struct _Job*, vref*);

typedef struct _Job
{
    JobFunction function;
    VM *vm;
    vref accessedFiles;
    vref modifiedFiles;
    uint argumentCount;
    int storeAt;
} Job;

nonnull Job *JobAdd(JobFunction function, VM *vm, const vref *arguments, uint argumentCount,
                      vref accessedFiles, vref modifiedFiles);
nonnull void JobDiscard(Job *job);
void JobExecute(Job *job);
