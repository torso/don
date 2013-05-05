extern void CacheInit(char *cacheDirectory, size_t cacheDirectoryLength);
extern void CacheDispose(void);
extern cacheref CacheGet(const byte *hash);
extern cacheref CacheGetFromFile(const char *path, size_t pathLength);
extern void CacheAddDependency(cacheref ref, const char *path, size_t length);
extern void CacheSetUptodate(cacheref ref, size_t outLength, size_t errLength,
                             char *output);
extern void CacheEchoCachedOutput(cacheref ref);
extern boolean CacheCheckUptodate(cacheref ref);
extern boolean CacheIsNewEntry(cacheref ref);
extern char *CacheGetFile(cacheref ref, size_t *length);
