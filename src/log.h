extern void LogInit(void);
extern void LogDispose(void);

extern boolean LogFlushParseErrors(void);
extern attrprintf(3, 0) void LogParseError(objectref filename, size_t line,
                                           const char *format, va_list ap);

/*
  Prints the specified text to standard output without any formatting. If the
  string does not end with a newline, one is added automatically.
*/
extern nonnull void LogPrintAutoNewline(const char *text, size_t length);
extern nonnull void LogPrintErrAutoNewline(const char *text, size_t length);

extern nonnull void LogPrintObjectAutoNewline(objectref object);
extern nonnull void LogPrintErrObjectAutoNewline(objectref object);
extern void LogAutoNewline(void);
extern void LogErrAutoNewline(void);

extern void LogSetPrefix(const char *prefix, size_t length);

#ifdef PIPE_H
extern PipeListener LogPipeOutListener;
extern PipeListener LogPipeErrListener;
#endif
