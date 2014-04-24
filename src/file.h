struct _FileEntry;
typedef struct _FileEntry FileEntry;

typedef struct
{
    time_t seconds;
    ulong fraction;
} filetime_t;

typedef struct
{
    int fd;
    const char *path;
    byte *data;
    size_t dataSize;
} File;

typedef struct
{
    mode_t mode;
    ino_t ino;
    uid_t uid;
    gid_t gid;
    off_t size;
    time_t mtime;
} FileStatus;

typedef void (*TraverseCallback)(const char *path, size_t length,
                                 void *userdata);


extern void FileInit(void);
extern void FileDisposeAll(void);

extern char *FileCreatePath(const char *restrict base, size_t baseLength,
                            const char *restrict path, size_t length,
                            const char *restrict extension, size_t extLength,
                            size_t *resultLength);
extern nonnull char *FileSearchPath(const char *name, size_t length,
                                    size_t *resultLength, bool executable);
extern nonnull const char *FileStripPath(const char *path, size_t *length);
extern nonnull void FileTraverseGlob(const char *pattern, size_t length,
                                     TraverseCallback callback, void *userdata);

extern nonnull void FileMarkModified(const char *path, size_t length);
extern nonnull const FileStatus *FileGetStatus(const char *path, size_t length);
extern nonnull bool FileHasChanged(const char *path, size_t length,
                                   const FileStatus *status);

extern nonnull void FileOpen(File *file, const char *path, size_t length);

/*
  Opens the file for reading. Returns false if the file does not exist.
*/
extern nonnull bool FileTryOpen(File *file, const char *path, size_t length);
extern nonnull void FileOpenAppend(File *file, const char *path, size_t length, bool truncate);
extern nonnull void FileClose(File *file);

extern nonnull size_t FileSize(File *file);
extern nonnull void FileRead(File *file, byte *buffer, size_t size);
extern nonnull void FileWrite(File *file, const byte *buffer, size_t size);

extern nonnull bool FileIsExecutable(const char *path, size_t length);
extern nonnull void FileDelete(const char *path, size_t length);
extern nonnull bool FileMkdir(const char *path, size_t length);
extern nonnull void FileCopy(const char *srcPath, size_t srcLength,
                             const char *dstPath, size_t dstLength);
extern nonnull void FileRename(const char *oldPath, size_t oldLength,
                               const char *newPath, size_t newLength);

/*
  Opens and mmaps the file. Fails if the file does not exist.
*/
extern nonnull void FileMMap(File *file, const byte **p, size_t *size);
extern nonnull void FileMUnmap(File *file);
