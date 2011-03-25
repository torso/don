extern void EnvInit(char **environ);
extern void EnvDispose(void);

extern void EnvGet(const char *name, size_t length,
                   const char **value, size_t *valueLength);

extern const char *const*EnvGetEnv(void);
extern const char *const*EnvCreateCopy(VM *vm, objectref overrides);
