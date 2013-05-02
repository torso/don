extern void StringPoolInit(void);
extern void StringPoolDispose(void);
extern nonnull vref StringPoolAdd(const char *token);
extern nonnull vref StringPoolAdd2(const char *token, size_t length);
