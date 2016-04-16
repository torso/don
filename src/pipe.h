void PipeInit(void);
void PipeDisposeAll(void);
void PipeProcess(void);


/* The returned handle may be used when calling other Pipe* functions.

   The file descriptor returned in fdWrite is for writing. It should be closed by the caller.
*/
nonnull int PipeCreate(int *fdWrite);
void PipeDispose(int pipe, vref *value);
void PipeConnect(int pipe, int fdTo);
