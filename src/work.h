typedef struct _Work
{
    nativefunctionref function;
    VM *vm;
} Work;

extern nonnull void WorkAdd(const Work *work);
