void CacheInit(const char *cacheDirectory, size_t cacheDirectoryLength,
               bool cacheDirectoryDotCache);
void CacheDispose(void);
void CacheGet(const byte *hash, bool echoCachedOutput, bool *uptodate, vref *path, vref *out);
void CacheSetUptodate(const char *path, size_t pathLength,
                      vref dependencies, vref output, vref data);
