typedef ErrorCode (*TraverseCallback)(fileref, void*);

extern ErrorCode FileInit(void);
extern void FileDisposeAll(void);

extern nonnull fileref FileAdd(const char *filename, size_t length);
extern void FileDispose(fileref file);

extern const char *FileGetName(fileref file);
extern size_t FileGetNameLength(fileref file);

/* TODO: Refcount mmap */
extern ErrorCode FileMMap(fileref file, const byte **p, size_t *size);
extern ErrorCode FileMUnmap(fileref file);

extern nonnull const char *FileFilename(const char *path, size_t *length);
extern ErrorCode FileTraverseGlob(const char *pattern,
                                  TraverseCallback callback,
                                  void *userdata);
