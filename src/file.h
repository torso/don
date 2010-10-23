#include <time.h>

typedef struct
{
    time_t seconds;
    ulong fraction;
} filetime_t;

typedef void (*TraverseCallback)(fileref, void*);


extern void FileInit(void);
extern void FileDisposeAll(void);

extern nonnull fileref FileAdd(const char *filename, size_t length);
extern nonnull fileref FileAddRelative(const char *base, size_t baseLength,
                                       const char *filename, size_t length);
extern void FileDispose(fileref file);

extern const char *FileGetNameBlob(fileref file);
extern const char *FileGetName(fileref file);
extern size_t FileGetNameLength(fileref file);
extern nonnull size_t FileGetSize(fileref file);
extern nonnull const byte *FileGetStatusBlob(fileref file);
extern pure size_t FileGetStatusBlobSize(void);

extern void FileOpenAppend(fileref file);
extern void FileCloseSync(fileref file);
extern nonnull void FileWrite(fileref file, const byte *data, size_t size);

/* TODO: Refcount mmap */
extern void FileMMap(fileref file, const byte **p, size_t *size,
                     boolean failOnFileNotFound);
extern void FileMUnmap(fileref file);

extern void FileDelete(fileref file);
extern void FileRename(fileref oldFile, fileref newFile);
extern void FileMkdir(fileref file);

extern nonnull const char *FileFilename(const char *path, size_t *length);
extern void FileTraverseGlob(const char *pattern,
                             TraverseCallback callback, void *userdata);
