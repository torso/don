#define STRINGPOOL_H

typedef int stringref;

extern ErrorCode StringPoolInit(void);
extern void StringPoolDispose(void);
extern nonnull stringref StringPoolAdd(const char *token);
extern nonnull stringref StringPoolAdd2(const char *token, size_t length);
extern pure const char *StringPoolGetString(stringref ref);
extern pure size_t StringPoolGetStringLength(stringref ref);
