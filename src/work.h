typedef struct _Work
{
    nativefunctionref function;
    vref condition;
    VM *vm;
    vref accessedFiles;
    vref modifiedFiles;
} Work;

extern void WorkInit(void);
extern void WorkDispose(void);
extern nonnull void WorkAdd(const Work *work);
extern nonnull void WorkDiscard(const VM *vm);
extern boolean WorkQueueEmpty(void);
extern void WorkExecute(void);
