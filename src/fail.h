extern noreturn attrprintf(1, 2) void Fail(const char *format, ...);
extern noreturn void FailErrno(boolean forked);
extern noreturn void FailOOM(void);
extern noreturn void FailIO(const char *message, const char *filename);
extern noreturn void FailIOErrno(const char *message, const char *filename, int error);
extern noreturn void FailVM(VM *vm);