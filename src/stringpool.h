extern ErrorCode StringPoolInit(void);
extern void StringPoolDispose(void);
extern nonnull stringref StringPoolAdd(const char *token);
extern nonnull stringref StringPoolAdd2(const char *token, size_t length);
extern const char *StringPoolGetString(stringref ref);
extern size_t StringPoolGetStringLength(stringref ref);
