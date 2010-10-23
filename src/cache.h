extern void CacheInit(void);
extern void CacheDispose(void);
extern void CacheGet(const byte *hash, cacheref *ref);
extern void CacheSetUptodate(cacheref ref);
extern void CacheAddDependency(cacheref ref, fileref file);
extern boolean CacheUptodate(cacheref ref);
extern boolean CacheIsNewEntry(cacheref ref);
extern fileref CacheGetFile(cacheref ref);
