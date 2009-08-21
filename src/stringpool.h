typedef uint stringref;

extern void StringPoolInit(void);
extern nonnull stringref StringPoolAdd(const char* token);
extern nonnull stringref StringPoolAdd2(const char* token, uint length);
extern pure const char* StringPoolGetString(stringref ref);
extern pure uint StringPoolGetStringLength(stringref ref);
