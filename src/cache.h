extern void CacheInit(void);
extern void CacheDispose(void);
extern cacheref CacheGet(const byte *hash);
extern cacheref CacheGetFromFile(fileref file);
extern void CacheAddDependency(cacheref ref, fileref file);
extern void CacheSetUptodate(cacheref ref, size_t outLength, size_t errLength,
                             char *output);
extern void CacheEchoCachedOutput(cacheref ref);
extern boolean CacheCheckUptodate(cacheref ref);
extern boolean CacheIsNewEntry(cacheref ref);
extern fileref CacheGetFile(cacheref ref);
