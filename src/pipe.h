void PipeInit(void);
void PipeDisposeAll(void);
void PipeProcess(void);


/* The returned handle (>= 0) may be used when calling other Pipe* functions.

   The file descriptor returned in fdWrite is for writing. It should be closed by the caller.
*/
nonnull int PipeCreateWrite(int *fdWrite);

/* The returned handle (>= 0) may be used when calling other Pipe* functions.

   Data should be added to *buffer and can be read through fdRead. fdRead should be closed by the
   caller.
*/
nonnull int PipeCreateRead(int *fdRead, bytevector **buffer, size_t bufferSize);
bool PipeIsOpen(int pipe);
void PipeDispose(int pipe, vref *value);
void PipeConnect(int pipe, int fd);
