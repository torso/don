extern void CacheInit(char *cacheDirectory, size_t cacheDirectoryLength);
extern void CacheDispose(void);
extern void CacheGet(const byte *hash, boolean echoCachedOutput,
                     boolean *uptodate, char **path, size_t *pathLength);
extern void CacheSetUptodate(const char *path, size_t pathLength,
                             vref dependencies, vref out, vref err);
