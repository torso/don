void LogInit(void);
void LogDispose(void);

nonnull void LogPrint(const char *text, size_t length);

/*
  Prints the specified text to standard output without any formatting. If the
  string does not end with a newline, one is added automatically.
*/
nonnull void LogPrintAutoNewline(const char *text, size_t length);

void LogPrintObjectAutoNewline(vref object);
void LogAutoNewline(void);

void LogSetPrefix(const char *prefix, size_t length);
