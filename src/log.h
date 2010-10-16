extern ErrorCode LogInit(void);
extern void LogDispose(void);

extern nonnull void LogParseError(fileref file, size_t line, const char *message);

/*
  Prints the specified text to standard output without any formatting.
*/
extern nonnull ErrorCode LogPrint(const char *text, size_t length);

/*
  Calls LogPrint after counting the number of characters in the zero terminated
  string.
*/
extern nonnull ErrorCode LogPrintSZ(const char *text);

/*
  Prints the specified text to standard output without any formatting. If the
  string does not end with a newline, one is added automatically.
*/
extern nonnull ErrorCode LogPrintAutoNewline(const char *text, size_t length);

extern nonnull ErrorCode LogPrintObjectAutoNewline(VM *vm, objectref object);
extern ErrorCode LogNewline(void);
extern ErrorCode LogAutoNewline(void);

extern void LogSetPrefix(const char *prefix, size_t length);

extern ErrorCode LogConsumePipes(int fdOut, int fdErr);

extern ErrorCode LogPushOutBuffer(boolean echo);
extern ErrorCode LogPushErrBuffer(boolean echo);
extern void LogGetOutBuffer(const byte **output, size_t *length);
extern void LogGetErrBuffer(const byte **output, size_t *length);
extern void LogPopOutBuffer(void);
extern void LogPopErrBuffer(void);
