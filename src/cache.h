extern ErrorCode CacheInit(void);
extern void CacheDispose(void);
extern ErrorCode CacheGet(const byte *hash, cacheref *ref);
extern ErrorCode CacheSetUptodate(cacheref ref);
extern ErrorCode CacheAddDependency(cacheref ref, fileref file);
extern boolean CacheUptodate(cacheref ref);
extern boolean CacheIsNewEntry(cacheref ref);
extern fileref CacheGetFile(cacheref ref);
