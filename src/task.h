extern noreturn void TaskFailErrno(boolean forked);
extern noreturn void TaskFailOOM(void);
extern noreturn void TaskFailIO(const char *filename);
extern noreturn void TaskFailVM(VM *vm);
