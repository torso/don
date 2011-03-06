typedef struct _Work
{
    nativefunctionref function;
    VM *vm;
} Work;

extern void WorkInit(void);
extern void WorkDispose(void);
extern nonnull void WorkAdd(const Work *work);
extern boolean WorkQueueEmpty(void);
extern void WorkExecute(void);
