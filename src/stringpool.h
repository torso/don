#define STRINGPOOL_H

typedef int stringref;

extern void StringPoolInit(void);
extern void StringPoolFree(void);
extern nonnull stringref StringPoolAdd(const char *token);
extern nonnull stringref StringPoolAdd2(const char *token, size_t length);
extern pure const char *StringPoolGetString(stringref ref);
extern pure size_t StringPoolGetStringLength(stringref ref);
