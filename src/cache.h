extern void CacheInit(void);
extern void CacheDispose(void);
extern cacheref CacheGet(const byte *hash);
extern cacheref CacheGetFromFile(fileref file);
extern void CacheSetUptodate(cacheref ref);
extern void CacheAddDependency(cacheref ref, fileref file);
extern boolean CacheUptodate(cacheref ref);
extern boolean CacheIsNewEntry(cacheref ref);
extern fileref CacheGetFile(cacheref ref);
