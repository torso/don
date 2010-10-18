#include <time.h>

typedef struct
{
    time_t seconds;
    ulong fraction;
} filetime_t;

typedef ErrorCode (*TraverseCallback)(fileref, void*);


extern ErrorCode FileInit(void);
extern void FileDisposeAll(void);

extern nonnull fileref FileAdd(const char *filename, size_t length);
extern nonnull fileref FileAddRelative(const char *base, size_t baseLength,
                                       const char *filename, size_t length);
extern void FileDispose(fileref file);

extern const char *FileGetNameBlob(fileref file);
extern const char *FileGetName(fileref file);
extern size_t FileGetNameLength(fileref file);
extern nonnull ErrorCode FileGetSize(fileref file, size_t *size);
extern nonnull ErrorCode FileGetStatusBlob(fileref file, const byte **blob);
extern pure size_t FileGetStatusBlobSize(void);

extern nonnull ErrorCode FileOpenAppend(fileref file);
extern nonnull ErrorCode FileWrite(fileref file, const byte *data,
                                    size_t size);

/* TODO: Refcount mmap */
extern ErrorCode FileMMap(fileref file, const byte **p, size_t *size);
extern ErrorCode FileMUnmap(fileref file);

extern ErrorCode FileMkdir(fileref file);

extern nonnull const char *FileFilename(const char *path, size_t *length);
extern ErrorCode FileTraverseGlob(const char *pattern,
                                  TraverseCallback callback,
                                  void *userdata);
