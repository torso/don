extern nonnull pure uint UtilHashString(const char *string, size_t length);
extern nonnull void UtilHexString(const byte *restrict data, size_t size,
                                  char *restrict output);
extern nonnull void UtilBase32(const byte *restrict data, size_t size,
                               char *restrict output);
extern size_t UtilCountNewlines(const char *text, size_t length);
