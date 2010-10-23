extern noreturn void TaskFailErrno(void);
extern noreturn void TaskFailOOM(void);
extern noreturn void TaskFailIO(const char *filename);
extern noreturn void TaskFailVM(VM *vm);
