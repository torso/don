#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <memory.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include "common.h"
#include "file.h"
#include "glob.h"
#include "stringpool.h"
#include "task.h"
#include "util.h"

#define INITIAL_FILE_SIZE 1024

#define FLAG_FREE_FILENAME 1

typedef struct
{
    size_t size;
    filetime_t mtime;
} StatusBlob;

struct _FileEntry;
typedef struct _FileEntry FileEntry;
struct _FileEntry
{
    fileref next;
    uint refCount;

    char *name;
    size_t nameLength;
    int flags;

    int fd;
    boolean append;

    boolean hasStat;
    mode_t mode;
    dev_t dev;
    ino_t ino;

    byte *data;
    uint dataRefCount;

    StatusBlob blob;
};

static uint *fileTable;
static FileEntry *fileIndex;
static uint fileIndexSize;
static uint freeFile;

static TraverseCallback globalCallback;
static void *globalUserdata;
static const char *globalPattern;
static size_t globalFilenamePrefixLength;
static FileEntry *globalFE;
static char *cwd;
static size_t cwdLength;


static void checkFile(fileref file)
{
    assert(file);
    assert(sizeFromRef(file) <= fileIndexSize);
    assert(fileIndex[sizeFromRef(file) - 1].refCount);
}

static FileEntry *getFileEntry(fileref file)
{
    checkFile(file);
    return &fileIndex[sizeFromRef(file) - 1];
}

static char *copyString(const char *restrict string, size_t length)
{
    char *restrict buffer = (char*)malloc(length + 1);
    memcpy(buffer, string, length);
    buffer[length] = 0;
    return buffer;
}

/* TODO: Handle backslashes */
static char *cleanFilename(char *filename, size_t length, size_t *resultLength)
{
    char *p;

    assert(length);
    p = filename + length;
    do
    {
        p--;
        if (*p == '/')
        {
            if (p[1] == '/')
            {
                /* Strip // */
                memmove(p, p + 1, length - (size_t)(p - filename));
                length--;
            }
            else if (p[1] == '.')
            {
                if (p[2] == '/')
                {
                    /* Strip /./ */
                    memmove(p, p + 2, length - (size_t)(p - filename) - 1);
                    length -= 2;
                }
                else if (!p[2])
                {
                    /* Strip /. */
                    p[1] = 0;
                    length--;
                }
                else if (p[2] == '.' && p[3] == '/' && !p[4])
                {
                    /* Strip /../ -> /.. */
                    p[3] = 0;
                    length--;
                }
            }
        }
    }
    while (p != filename);

    while (length >= 3 && p[0] == '/' && p[1] == '.' && p[2] == '.')
    {
        if (!p[3])
        {
            p[1] = 0;
            length = 1;
            break;
        }
        if (p[3] == '/')
        {
            memmove(p, p + 3, length - 2);
            length -= 3;
            continue;
        }
        break;
    }
    *resultLength = length;
    return filename;
}

static char *getAbsoluteFilename(
    const char *restrict base, size_t baseLength,
    const char *restrict path, size_t length,
    const char *restrict extension, size_t extLength,
    size_t *resultLength)
{
    const char *restrict base2 = null;
    size_t base2Length = 0;
    char *restrict buffer;
    size_t i;

    if (length && path[0] == '/')
    {
        baseLength = 0;
    }
    else if (!base)
    {
        base = cwd;
        baseLength = cwdLength;
    }
    else if (!baseLength || base[0] != '/')
    {
        base2 = cwd;
        base2Length = cwdLength;
    }

    buffer = (char*)malloc(base2Length + baseLength + length + extLength + 3);
    memcpy(buffer, base2, base2Length);
    memcpy(buffer + base2Length, base, baseLength);
    buffer[base2Length + baseLength] = '/';
    memcpy(&buffer[base2Length + baseLength + 1], path, length);
    buffer[base2Length + baseLength + length + 1] = 0;
    cleanFilename(buffer, base2Length + baseLength + length + 1, resultLength);

    if (extension &&
        buffer[*resultLength - 1] != '/' &&
        (*resultLength < 3 ||
         buffer[*resultLength - 3] != '/' ||
         buffer[*resultLength - 2] != '.' ||
         buffer[*resultLength - 1] != '.'))
    {
        if (extLength && extension[0] == '.')
        {
            extension++;
            extLength--;
        }
        for (i = *resultLength;; i--)
        {
            if (buffer[i] == '/')
            {
                if (extLength)
                {
                    buffer[*resultLength] = '.';
                    memcpy(buffer + *resultLength + 1, extension, extLength);
                    *resultLength += extLength + 1;
                    buffer[*resultLength] = 0;
                }
                break;
            }
            if (buffer[i] == '.')
            {
                if (extLength)
                {
                    memcpy(buffer + i + 1, extension, extLength);
                    buffer[i + extLength + 1] = 0;
                    *resultLength = i + 1 + extLength;
                }
                else
                {
                    buffer[i] = 0;
                    *resultLength = i;
                }
                break;
            }
        }
    }
    return buffer;
}

static char *stripFilename(const char *filename, size_t length,
                           size_t *resultLength)
{
    const char *p;
    char *path;

    /* TODO: Handle filenames ending with /. and /.. */
    if (filename[length - 1] == '/')
    {
        return null;
    }
    for (p = filename + length; p >= filename; p--)
    {
        if (*p == '/')
        {
            length = (size_t)(p - filename);
            path = (char*)malloc(length + 1);
            memcpy(path, filename, length);
            path[length] = 0;
            *resultLength = length;
            return path;
        }
    }
    return null;
}

static void createDirectory(const char *path, size_t length, boolean mutablePath)
{
    struct stat s;
    char *p;
    char *end;
    uint level = 0;

    assert(length); /* TODO: Error handling. */
    assert(!mutablePath || !path[length]);
    if (path[length - 1] == '/')
    {
        length--;
    }
    if (mutablePath)
    {
        p = (char*)path;
    }
    else
    {
        p = (char*)malloc(length + 1);
        memcpy(p, path, length);
        p[length] = 0;
    }
    end = p + length;

    for (;;)
    {
        if (!stat(p, &s))
        {
            if (S_ISDIR(s.st_mode))
            {
                break;
            }
            if (p != path)
            {
                free(p);
            }
            errno = ENOTDIR;
            TaskFailIO(p);
        }
        else if (errno != ENOENT)
        {
            TaskFailIO(p);
        }
        for (end--; *end != '/'; end--)
        {
            assert(end != p); /* TODO: Error handling. */
        }
        *end = 0;
        level++;
    }
    while (level)
    {
        *end = '/';
        end += strlen(end);
        level--;
        if (mkdir(p, S_IRWXU | S_IRWXG | S_IRWXO))
        {
            TaskFailIO(p);
        }
    }
    if (p != path)
    {
        free(p);
    }
}

static void fileMUnmap(FileEntry *fe)
{
    int error;

    assert(fe->data);
    assert(fe->dataRefCount);
    if (!--fe->dataRefCount)
    {
        error = munmap(fe->data, fe->blob.size);
        fe->data = null;
        assert(!error);
    }
}

static void fileClose(FileEntry *fe)
{
    assert(!fe->data);
    if (fe->fd)
    {
        close(fe->fd);
        fe->fd = 0;
    }
}

static void fileOpen(FileEntry *fe, boolean append, boolean failOnFileNotFound)
{
    char *path;
    size_t length;

    if (append)
    {
        if (fe->fd)
        {
            if (!fe->append)
            {
                fileClose(fe);
            }
        }
    }
    if (fe->fd)
    {
        return;
    }
    fe->fd = open(fe->name,
                  O_CLOEXEC |
                  (append ? O_CREAT | O_WRONLY | O_APPEND : O_RDONLY),
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fe->fd == -1)
    {
        fe->fd = 0;
        if (append && errno == ENOENT)
        {
            path = stripFilename(fe->name, fe->nameLength, &length);
            if (path)
            {
                createDirectory(path, length, true);
                free(path);
                fileOpen(fe, append, failOnFileNotFound);
                return;
            }
        }
        if (failOnFileNotFound || errno != ENOENT)
        {
            TaskFailIO(fe->name);
        }
    }
    fe->append = append;
}

static void fileSetStat(FileEntry *fe, const struct stat *s)
{
    fe->hasStat = true;
    fe->blob.size = (size_t)s->st_size;
    fe->mode = s->st_mode;
    fe->dev = s->st_dev;
    fe->ino = s->st_ino;
    fe->blob.mtime.seconds = s->st_mtime;
    fe->blob.mtime.fraction = (ulong)s->st_mtim.tv_nsec;
}

static boolean fileStat(FileEntry *fe, boolean failOnFileNotFound)
{
    struct stat s;

    if (fe->hasStat)
    {
        return true;
    }
    if (fe->fd)
    {
        if (fstat(fe->fd, &s))
        {
            TaskFailIO(fe->name);
        }
    }
    else if (stat(fe->name, &s))
    {
        if (!failOnFileNotFound && errno == ENOENT)
        {
            return false;
        }
        TaskFailIO(fe->name);
    }
    fileSetStat(fe, &s);
    return true;
}

static boolean fileLStat(FileEntry *fe, struct stat *s)
{
    if (lstat(fe->name, s))
    {
        if (errno == ENOENT)
        {
            return false;
        }
        TaskFailIO(fe->name);
    }
    if (!S_ISLNK(s->st_mode))
    {
        fileSetStat(fe, s);
    }
    return true;
}

static void fileMMap(FileEntry *fe, const byte **p, size_t *size,
                     boolean failOnFileNotFound)
{
    if (!fe->data)
    {
        fileOpen(fe, false, failOnFileNotFound);
        if (!fe->fd)
        {
            *p = null;
            return;
        }
        fileStat(fe, true);
        fe->data = (byte*)mmap(null, fe->blob.size, PROT_READ, MAP_PRIVATE,
                               fe->fd, 0);
        if (fe->data == (byte*)-1)
        {
            fe->data = null;
            /* TODO: Read file fallback */
            TaskFailIO(fe->name);
        }
    }
    fe->dataRefCount++;
    *p = fe->data;
    *size = fe->blob.size;
}

static void fileWrite(FileEntry *fe, const byte *data, size_t size)
{
    ssize_t written;

    assert(fe->fd);
    assert(fe->append);
    while (size)
    {
        written = write(fe->fd, data, size);
        if (written < 0)
        {
            TaskFailIO(fe->name);
        }
        assert((size_t)written <= size);
        size -= (size_t)written;
    }
}

static void initFileIndex(uint start)
{
    uint index = start;

    for (index = start; index < fileIndexSize; index++)
    {
        fileIndex[index].refCount = 0;
        fileIndex[index].next = index + 2;
    }
    fileIndex[fileIndexSize - 1].next = 0;
    freeFile = start + 1;
}

static void disposeFile(FileEntry *fe)
{
    FileEntry *prev;
    uint slot = UtilHashString(fe->name, fe->nameLength) & (fileIndexSize - 1);
    uint file = (uint)(fe - fileIndex) + 1;

    if (fe->flags & FLAG_FREE_FILENAME)
    {
        free((void*)fe->name);
    }
    if (fe->data)
    {
        fileMUnmap(fe);
        fileClose(fe);
    }
    if (fileTable[slot] == file)
    {
        fileTable[slot] = fe->next;
    }
    else
    {
        prev = getFileEntry(fileTable[slot]);
        for (;;)
        {
            if (prev->next == file)
            {
                prev->next = fe->next;
                break;
            }
            prev = getFileEntry(prev->next);
        }
    }
    fe->next = freeFile;
    freeFile = file;
}

static fileref addFile(char *filename, size_t filenameLength,
                       boolean filenameOwner)
{
    FileEntry *fe;
    fileref file;
    uint oldSize;
    uint slot;
    uint i;

    assert(filename);
    assert(!filename[filenameLength]);

    if (!freeFile)
    {
        oldSize = fileIndexSize;
        fileIndexSize *= 2;
        fileIndex = (FileEntry*)realloc(fileIndex, fileIndexSize * sizeof(FileEntry));
        free(fileTable);
        fileTable = (uint*)calloc(fileIndexSize, sizeof(uint));
        i = oldSize;
        while (i--)
        {
            fe = &fileIndex[i];
            if (fe->refCount)
            {
                slot = UtilHashString(fe->name, fe->nameLength) & (fileIndexSize - 1);
                fe->next = fileTable[slot];
                fileTable[slot] = i + 1;
            }
        }
        initFileIndex(oldSize);
    }
    slot = UtilHashString(filename, filenameLength) & (fileIndexSize - 1);
    file = fileTable[slot];
    while (file)
    {
        fe = getFileEntry(file);
        if (filenameLength == fe->nameLength &&
            !memcmp(filename, fe->name, filenameLength))
        {
            if (filenameOwner)
            {
                free(filename);
            }
            return file;
        }
        file = fe->next;
    }

    fe = &fileIndex[freeFile - 1];
    assert(!fe->refCount);
    file = refFromUint(freeFile);
    freeFile = fe->next;
    fe->next = fileTable[slot];
    fe->name = filename;
    fe->nameLength = filenameLength;
    fe->flags = filenameOwner ? FLAG_FREE_FILENAME : 0;
    fe->refCount = 1;
    fe->fd = 0;
    fe->hasStat = false;
    fe->data = null;
    fe->dataRefCount = 0;
    fileTable[slot] = file;
    return file;
}


void FileInit(void)
{
    char *buffer;

    fileTable = (uint*)calloc(INITIAL_FILE_SIZE, sizeof(uint));
    fileIndexSize = INITIAL_FILE_SIZE;
    fileIndex = (FileEntry*)malloc(INITIAL_FILE_SIZE * sizeof(FileEntry));
    initFileIndex(0);

    cwd = getcwd(null, 0);
    if (!cwd)
    {
        TaskFailOOM();
    }
    cwdLength = strlen(cwd);
    assert(cwdLength);
    assert(cwd[0] == '/');
    if (cwd[cwdLength - 1] != '/')
    {
        buffer = (char*)realloc(cwd, cwdLength + 2);
        buffer[cwdLength] = '/';
        buffer[cwdLength + 1] = '/';
        cwd = buffer;
        cwdLength++;
    }
}

void FileDisposeAll(void)
{
    FileEntry *fe;
    uint i = fileIndexSize;
    while (i--)
    {
        fe = &fileIndex[i];
        if (fe->refCount)
        {
            fe->refCount = 0;
            disposeFile(fe);
        }
    }
    free(fileTable);
    free(fileIndex);
    free(cwd);
}


fileref FileAdd(const char *filename, size_t length)
{
    char *name = getAbsoluteFilename(null, 0, filename, length, null, 0, &length);
    return addFile(name, length, true);
}

fileref FileAddRelative(const char *base, size_t baseLength,
                        const char *filename, size_t length)
{
    char *name = getAbsoluteFilename(base, baseLength, filename, length, null, 0,
                                     &length);
    return addFile(name, length, true);
}

fileref FileAddRelativeExt(const char *base, size_t baseLength,
                           const char *filename, size_t length,
                           const char *extension, size_t extLength)
{
    char *name = getAbsoluteFilename(base, baseLength, filename, length,
                                     extension, extLength, &length);
    return addFile(name, length, true);
}

fileref FileAddSearch(const char *filename, size_t length,
                      const char *path, size_t pathLength)
{
    fileref file;
    const char *stop = path + pathLength;
    const char *p;

    if (filename[0] == '/')
    {
        return FileAdd(filename, length);
    }
    while (path < stop)
    {
        for (p = path; p < stop && *p != ':'; p++);
        file = FileAddRelative(path, (size_t)(p - path), filename, length);
        if (FileIsExecutable(file))
        {
            return file;
        }
        path = p + 1;
    }
    return 0;
}

fileref FileAddSearchPath(const char *filename, size_t length)
{
    const char *path = getenv("PATH");
    if (path == null)
    {
        path = "";
    }
    return FileAddSearch(filename, length, path, strlen(path));
}

void FileDispose(fileref file)
{
    FileEntry *fe = getFileEntry(file);
    assert(fe->refCount);
    if (!--fe->refCount)
    {
        disposeFile(fe);
    }
}


const char *FileGetName(fileref file)
{
    return getFileEntry(file)->name;
}

size_t FileGetNameLength(fileref file)
{
    return getFileEntry(file)->nameLength;
}

size_t FileGetSize(fileref file)
{
    FileEntry *restrict fe = getFileEntry(file);
    fileStat(fe, true);
    return fe->blob.size;
}

boolean FileIsExecutable(fileref file)
{
    FileEntry *fe = getFileEntry(file);
    if (!fileStat(fe, false))
    {
        return false;
    }
    return S_ISREG(fe->mode) && (fe->mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

const byte *FileGetStatusBlob(fileref file)
{
    FileEntry *fe = getFileEntry(file);
    fileStat(fe, true);
    return (const byte*)&fe->blob;
}

size_t FileGetStatusBlobSize(void)
{
    return sizeof(StatusBlob);
}

boolean FileHasChanged(fileref file, const byte *blob)
{
    return memcmp(FileGetStatusBlob(file), blob, FileGetStatusBlobSize()) != 0;
}


void FileOpenAppend(fileref file)
{
    fileOpen(getFileEntry(file), true, true);
}

void FileClose(fileref file)
{
    fileClose(getFileEntry(file));
}

void FileCloseSync(fileref file)
{
    FileEntry *fe = getFileEntry(file);
    if (fe->fd)
    {
        /* Sync is best-effort only. Ignore errors. */
        fdatasync(fe->fd);

        fileClose(fe);
    }
}

void FileWrite(fileref file, const byte *data, size_t size)
{
    fileWrite(getFileEntry(file), data, size);
}


void FileMMap(fileref file, const byte **p, size_t *size,
              boolean failOnFileNotFound)
{
    fileMMap(getFileEntry(file), p, size, failOnFileNotFound);
}

void FileMUnmap(fileref file)
{
    fileMUnmap(getFileEntry(file));
}


static void fileCopySingle(FileEntry *src, FileEntry *dst)
{
    const byte *p;
    size_t size;

    if (src == dst)
    {
        return;
    }
    assert(src->hasStat);
    if (fileStat(dst, false))
    {
        if (src->dev == dst->dev && src->ino == dst->ino)
        {
            return;
        }
    }
    fileClose(dst);
    dst->append = true;
    dst->fd = open(dst->name,
                   O_CLOEXEC | O_CREAT | O_WRONLY | O_APPEND | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (dst->fd == -1)
    {
        TaskFailIO(dst->name);
    }
    if (src->blob.size)
    {
        size = 0;
        fileMMap(src, &p, &size, true);
        fileWrite(dst, p, size);
        fileMUnmap(src);
    }
    fileClose(dst);
}

static int fileCopyCallback(const char *filename, const struct stat *s,
                            int flags, struct FTW *f)
{
    FileEntry *srcFE;
    FileEntry *dstFE;
    size_t length;

    if (!f->level)
    {
        return 0;
    }
    length = (size_t)f->base + strlen(filename + f->base);
    srcFE = getFileEntry(addFile(copyString(filename, length), length, true));
    fileSetStat(srcFE, s);
    dstFE = getFileEntry(FileAddRelative(globalFE->name, globalFE->nameLength,
                                         filename + globalFilenamePrefixLength,
                                         length - globalFilenamePrefixLength));
    if (flags == FTW_D)
    {
        if (mkdir(dstFE->name, S_IRWXU | S_IRWXG | S_IRWXO) &&
            errno != EEXIST)
        {
            TaskFailIO(dstFE->name);
        }
        return 0;
    }
    if (flags == FTW_F)
    {
        fileCopySingle(srcFE, dstFE);
        return 0;
    }
    TaskFailIO(filename);
}

void FileCopy(fileref srcFile, fileref dstFile)
{
    FileEntry *srcFE;
    FileEntry *dstFE;
    struct stat s;
    const char *name;
    size_t size;

    if (srcFile == dstFile)
    {
        return;
    }
    srcFE = getFileEntry(srcFile);
    dstFE = getFileEntry(dstFile);
    fileOpen(srcFE, false, true);
    fileStat(srcFE, true);
    if (S_ISDIR(srcFE->mode))
    {
        if (fileLStat(dstFE, &s))
        {
            if (S_ISLNK(s.st_mode))
            {
                if (!fileStat(dstFE, false))
                {
                    TaskFail("Cannot copy directory '%s' through dangling symlink '%s'.\n",
                             srcFE->name, dstFE->name);
                }
            }
            if (!S_ISDIR(dstFE->mode))
            {
                TaskFail("Cannot copy directory '%s' to non-directory '%s'.\n",
                         srcFE->name, dstFE->name);
            }
            size = srcFE->nameLength;
            name = FileFilename(srcFE->name, &size);
            dstFE = getFileEntry(FileAddRelative(
                                     dstFE->name, dstFE->nameLength,
                                     name, size));
        }
        if (mkdir(dstFE->name, S_IRWXU | S_IRWXG | S_IRWXO) &&
            errno != EEXIST)
        {
            TaskFailIO(dstFE->name);
        }
        globalFE = dstFE;
        globalFilenamePrefixLength = srcFE->name[srcFE->nameLength - 1] == '/' ?
            srcFE->nameLength : srcFE->nameLength + 1;
        if (nftw(srcFE->name, fileCopyCallback, 10, 0))
        {
            TaskFailIO(srcFE->name);
        }
        return;
    }
    if (fileLStat(dstFE, &s))
    {
        if (S_ISLNK(s.st_mode))
        {
            if (!fileStat(dstFE, false))
            {
                TaskFail("Cannot copy '%s' through dangling symlink '%s'.\n",
                         srcFE->name, dstFE->name);
            }
        }
        if (S_ISDIR(dstFE->mode))
        {
            size = srcFE->nameLength;
            name = FileFilename(srcFE->name, &size);
            fileCopySingle(srcFE,
                           getFileEntry(FileAddRelative(
                                            dstFE->name, dstFE->nameLength,
                                            name, size)));
            return;
        }
    }
    fileCopySingle(srcFE, dstFE);
}

static int fileDeleteCallback(const char *filename, const struct stat *s unused,
                              int flags unused, struct FTW *f unused)
{
    if (remove(filename) && errno != ENOENT)
    {
        TaskFailIO(filename);
    }
    return 0;
}

void FileDelete(fileref file)
{
    FileEntry *fe = getFileEntry(file);
    fileClose(fe);
    if (!remove(fe->name) || errno == ENOENT)
    {
        return;
    }
    if (errno == ENOTEMPTY)
    {
        if (!nftw(fe->name, fileDeleteCallback, 10, FTW_PHYS | FTW_MOUNT | FTW_DEPTH))
        {
            return;
        }
    }
    TaskFailIO(fe->name);
}

void FileRename(fileref oldFile, fileref newFile, boolean failOnFileNotFound)
{
    FileEntry *oldFE = getFileEntry(oldFile);
    FileEntry *newFE = getFileEntry(newFile);
    fileClose(oldFE);
    fileClose(newFE);
    if (!rename(oldFE->name, newFE->name))
    {
        return;
    }
    if (errno == EXDEV)
    {
        FileDelete(newFile);
        FileCopy(oldFile, newFile);
        FileDelete(oldFile);
        return;
    }
    if (errno == ENOTEMPTY || errno == EEXIST || errno == EISDIR)
    {
        FileDelete(newFile);
        if (!rename(oldFE->name, newFE->name))
        {
            return;
        }
    }
    if (failOnFileNotFound || errno != ENOENT)
    {
        TaskFailIO(oldFE->name);
    }
}

void FileMkdir(fileref file)
{
    FileEntry *fe = getFileEntry(file);
    createDirectory(fe->name, fe->nameLength, false);
}


static int globTraverse(const char *filename, const struct stat *info unused,
                        int flags unused)
{
    size_t length = strlen(filename);

    if (length < globalFilenamePrefixLength)
    {
        return 0;
    }
    if (!GlobMatch(globalPattern, filename + globalFilenamePrefixLength))
    {
        return 0;
    }
    globalCallback(addFile(copyString(filename, length), length, true),
                   globalUserdata);
    return 0;
}

const char *FileFilename(const char *path, size_t *length)
{
    const char *current = path + *length;
    while (current > path && current[-1] != '/')
    {
        current--;
    }
    *length = (size_t)(path + *length - current);
    return current;
}

void FileTraverseGlob(const char *pattern,
                      TraverseCallback callback, void *userdata)
{
    const char *slash = null;
    const char *asterisk = null;
    const char *p;
    char *filename;
    size_t length;
    int error;

    for (p = pattern; *p; p++)
    {
        if (*p == '/')
        {
            slash = p;
        }
        else if (*p == '*')
        {
            asterisk = p;
            break;
        }
    }
    length = (size_t)(p - pattern) + strlen(p);
    assert(length == strlen(pattern));

    if (!asterisk)
    {
        callback(FileAdd(pattern, length), userdata);
        return;
    }

    if (slash)
    {
        filename = getAbsoluteFilename(null, 0,
                                       pattern, (size_t)(slash - pattern),
                                       null, 0,
                                       &length);
    }
    else
    {
        filename = cwd;
        length = cwdLength;
    }

    globalCallback = callback;
    globalUserdata = userdata;
    if (slash)
    {
        globalPattern = slash + 1;
        globalFilenamePrefixLength = strlen(filename) + 1;
    }
    else
    {
        globalPattern = pattern;
        globalFilenamePrefixLength = cwdLength + 1;
    }
    error = ftw(filename, globTraverse, 10);
    if (slash)
    {
        free(filename);
    }
    if (error == -1)
    {
        TaskFailIO(filename);
    }
}
