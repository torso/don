extern void LogInit(void);
extern void LogDispose(void);

/*
  Prints the specified text to standard output without any formatting. If the
  string does not end with a newline, one is added automatically.
*/
extern nonnull void LogPrintAutoNewline(const char *text, size_t length);

extern nonnull void LogPrintObjectAutoNewline(vref object);
extern void LogAutoNewline(void);

extern void LogSetPrefix(const char *prefix, size_t length);

struct _PipeListener;
extern struct _PipeListener LogPipeOutListener;
