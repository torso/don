void PipeInit(void);
void PipeDisposeAll(void);
void PipeProcess(void);


/* The returned file descriptor may be used when calling other Pipe* functions. It is read by
   PipeProcess() and mustn't be read by anything else.

   The file descriptor returned in fdWrite is for writing and is suitable for giving to a
   subprocess. It should be closed by the caller.
 */
nonnull int PipeCreate(int *fdWrite);
void PipeDispose(int fd, vref *value);
void PipeConnect(int fdFrom, int fdTo);
