noreturn attrprintf(1, 2) void Fail(const char *format, ...);
noreturn void FailErrno(bool forked);
noreturn void FailOOM(void);
noreturn void FailIO(const char *message, const char *filename);
noreturn void FailIOErrno(const char *message, const char *filename, int error);
