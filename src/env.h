void EnvInit(char **environ);
void EnvDispose(void);

void EnvGet(const char *name, size_t length, const char **value, size_t *valueLength);

const char *const*EnvGetEnv(void);
const char *const*EnvCreateCopy(vref overrides);
