void CacheInit(char *cacheDirectory, size_t cacheDirectoryLength);
void CacheDispose(void);
void CacheGet(const byte *hash, bool echoCachedOutput,
              bool *uptodate, char **path, size_t *pathLength, vref *out);
void CacheSetUptodate(const char *path, size_t pathLength,
                      vref dependencies, vref out, vref err, vref data);
