extern nonnull void UtilHexString(const byte *restrict data, size_t size,
                                  char *restrict output);
extern nonnull void UtilBase32(const byte *restrict data, int size,
                               char *restrict output);
/* data and output may overlap. */
extern nonnull void UtilDecodeBase32(const char *data, int size, byte *output);
extern size_t UtilCountNewlines(const char *text, size_t length);
