#include <time.h>

struct _TreeEntry;
typedef struct _TreeEntry TreeEntry;

typedef struct
{
    time_t seconds;
    ulong fraction;
} filetime_t;

typedef struct
{
    TreeEntry *te;
    int mmapRefCount;
} File;

typedef void (*TraverseCallback)(const char *path, size_t length,
                                 void *userdata);


extern void FileInit(void);
extern void FileDisposeAll(void);

extern char *FileCreatePath(const char *restrict base, size_t baseLength,
                            const char *restrict path, size_t length,
                            const char *restrict extension, size_t extLength,
                            size_t *resultLength);
extern nonnull char *FileSearchPath(const char *name, size_t length,
                                    size_t *resultLength);
extern nonnull const char *FileStripPath(const char *path, size_t *length);
extern nonnull void FileTraverseGlob(const char *pattern, size_t length,
                                     TraverseCallback callback, void *userdata);

extern nonnull void FilePinDirectory(const char *path, size_t length);
extern nonnull void FileUnpinDirectory(const char *path, size_t length);
extern nonnull void FileMarkModified(const char *path, size_t length);
extern nonnull const byte *FileStatusBlob(const char *path, size_t length);
extern pureconst size_t FileStatusBlobSize(void);
extern nonnull boolean FileHasChanged(const char *path, size_t length,
                                      const byte *blob);

/*
  Returns true if the specified File struct is referencing an opened file. This
  only queries the state of the struct, not whether any actual file is open. The
  struct must either have been used to open a file in the past, or it must be
  completely zero.
*/
extern nonnull pureconst boolean FileIsOpen(File *file);

extern nonnull void FileOpen(File *file, const char *path, size_t length);

/*
  Opens the file for reading. Returns false if the file does not exist.
*/
extern nonnull boolean FileTryOpen(File *file, const char *path, size_t length);
extern nonnull void FileOpenAppend(File *file, const char *path, size_t length);
extern nonnull void FileClose(File *file);

extern nonnull size_t FileSize(File *file);
extern nonnull void FileRead(File *file, byte *buffer, size_t size);
extern nonnull void FileWrite(File *file, const byte *buffer, size_t size);

extern nonnull boolean FileIsExecutable(const char *path, size_t length);
extern nonnull void FileDelete(const char *path, size_t length);
extern nonnull void FileMkdir(const char *path, size_t length);
extern nonnull void FileCopy(const char *srcPath, size_t srcLength,
                             const char *dstPath, size_t dstLength);
extern nonnull void FileRename(const char *oldPath, size_t oldLength,
                               const char *newPath, size_t newLength);

/*
  Opens and mmaps the file. Fails if the file does not exist.
*/
extern nonnull void FileMMap(File *file, const byte **p, size_t *size);
extern nonnull void FileMUnmap(File *file);
