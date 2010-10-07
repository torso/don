typedef ErrorCode (*TraverseCallback)(fileref, void*);

extern ErrorCode FileIndexInit(void);
extern void FileIndexDispose(void);

extern nonnull fileref FileIndexOpen(const char *filename);
extern void FileIndexClose(fileref file);
extern pure const char *FileIndexGetName(fileref file);
extern pure const byte *FileIndexGetContents(fileref file);
extern pure size_t FileIndexGetSize(fileref file);

extern nonnull const char *FileIndexFilename(const char *path, size_t *length);
extern ErrorCode FileIndexTraverseGlob(const char *pattern,
                                       TraverseCallback callback,
                                       void *userdata);
