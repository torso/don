extern void StringPoolInit(void);
extern void StringPoolDispose(void);
extern nonnull objectref StringPoolAdd(const char *token);
extern nonnull objectref StringPoolAdd2(const char *token, size_t length);
