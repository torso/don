extern ErrorCode CacheInit(void);
extern void CacheDispose(void);
extern ErrorCode CacheGet(const byte *hash, cacheref *ref);
extern boolean CacheUptodate(cacheref ref);
extern fileref CacheGetDirectory(cacheref ref);
