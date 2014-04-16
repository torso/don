#define _XOPEN_SOURCE 700
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include "common.h"
#include "bytevector.h"
#include "env.h"
#include "fail.h"
#include "file.h"
#include "glob.h"
#include "hash.h"

#define FILE_FREE_STRUCT 1
#define FILE_FREE_FILENAME 2

struct _FileEntry
{
    uint refCount;
    uint dataRefCount;
    int fd;
    mode_t mode;
    ino_t ino;
    dev_t dev;
    boolean hasStat;
    FileStatus status;
    byte *data;
    size_t pathLength;
    char path[1];
};

static FileEntry *table[0x400];
static const uint tableMask = sizeof(table) / sizeof(*table) - 1;
static char *cwd;
static size_t cwdLength;


static boolean pathIsDirectory(const char *path, size_t length)
{
    return path[length] == '/';
}

static size_t parentPathLength(const char *path, size_t length)
{
    assert(length > 1);
    assert(*path == '/');
    while (path[--length] != '/');
    return max(length, 1);
}


static uint feIndex(const char *path, size_t length)
{
    assert(path);
    assert(length);
    assert(*path == '/');
    return HashString(path, length) & tableMask;
}

static void initEntry(FileEntry *fe, const char *path, size_t length)
{
    fe->refCount = 1;
    fe->dataRefCount = 0;
    fe->fd = 0;
    fe->hasStat = 0;
    fe->data = null;
    fe->pathLength = length;
    memcpy(fe->path, path, length);
    fe->path[length] = 0;
}

static FileEntry *createEntry(uint index, const char *path, size_t length)
{
    FileEntry *fe = (FileEntry*)malloc(sizeof(*fe) - sizeof(fe->path) + length + 1);
    table[index] = fe;
    initEntry(fe, path, length);
    return fe;
}

static void clearTableEntry(uint index)
{
    assert(table[index]->refCount);
    if (!--table[index]->refCount)
    {
        free(table[index]);
    }
    table[index] = null;
}

static FileEntry *feEntry(const char *path, size_t length)
{
    uint index = feIndex(path, length);
    FileEntry *fe = table[index];

    if (!fe)
    {
        return createEntry(index, path, length);
    }
    if (fe->refCount > 1)
    {
        fe->refCount--;
        return createEntry(index, path, length);
    }
    if (fe->pathLength != length)
    {
        if (fe->pathLength < length) /* TODO: Track allocated length? */
        {
            clearTableEntry(index);
            return createEntry(index, path, length);
        }
        initEntry(fe, path, length);
    }
    else if (memcmp(fe->path, path, length))
    {
        initEntry(fe, path, length);
    }
    return fe;
}

static void feStatat(FileEntry *fe, int fdParent, const char *path)
{
    if (!fe->hasStat)
    {
        struct stat s;
        struct stat s2;

        fe->hasStat = true;
        memset(&fe->status, 0, sizeof(fe->status));
        if (fe->fd)
        {
            if (fstat(fe->fd, &s))
            {
                FailIO("Error accessing file", fe->path);
            }
        }
        else
        {
            if (
#ifdef HAVE_OPENAT
                    fstatat(fdParent, path, &s, AT_SYMLINK_NOFOLLOW)
#else
                    lstat(fe->path, &s)
#endif
                )
            {
                if (errno != ENOENT)
                {
                    FailIO("Error accessing file", fe->path);
                }
                return;
            }
            if (S_ISLNK(s.st_mode))
            {
                /* TODO: Maybe do something more clever with symlinks. */
                if (
#ifdef HAVE_OPENAT
                        fstatat(fdParent, path, &s2, 0)
#else
                        stat(fe->path, &s2)
#endif
                    )
                {
                    if (errno != ENOENT)
                    {
                        FailIO("Error accessing file", fe->path);
                    }
                }
                else
                {
                    s = s2;
                }
            }
        }

        fe->mode = s.st_mode;
        fe->dev = s.st_dev;
        fe->ino = s.st_ino;
        fe->status.exists = true;
        fe->status.size = (size_t)s.st_size;
        fe->status.mtime.seconds = s.st_mtime;
        fe->status.mtime.fraction = (ulong)s.st_mtim.tv_nsec;
    }
}

static void feStat(FileEntry *fe)
{
    feStatat(fe, AT_FDCWD, fe->path);
}

static void feClose(FileEntry *fe)
{
    assert(fe->fd);
    assert(!fe->data);
    close(fe->fd);
    fe->fd = 0;
}

static void feOpen(FileEntry *fe, int flags)
{
    assert(!fe->fd);
    fe->fd = open(fe->path, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fe->fd == -1)
    {
        fe->fd = 0;
        if (errno == ENOENT)
        {
            assert(!fe->hasStat || !fe->status.exists || (flags & O_CREAT));
            fe->hasStat = true;
            memset(&fe->status, 0, sizeof(fe->status));
        }
        return;
    }
}

static boolean feExists(FileEntry *fe)
{
    feStat(fe);
    return fe->status.exists;
}

static boolean feIsFile(FileEntry *fe)
{
    return feExists(fe) && S_ISREG(fe->mode);
}

static boolean feIsDirectory(FileEntry *fe)
{
    return feExists(fe) && S_ISDIR(fe->mode);
}

static size_t feSize(FileEntry *fe)
{
    feStat(fe);
    return fe->status.size;
}

static void feWrite(FileEntry *fe, const byte *data, size_t size)
{
    ssize_t written;

    assert(fe->fd);
    fe->hasStat = false;
    while (size)
    {
        written = write(fe->fd, data, size);
        if (written < 0)
        {
            FailIO("Error writing to file", fe->path);
        }
        assert((size_t)written <= size);
        size -= (size_t)written;
        data += written;
    }
}

static void feMMap(FileEntry *fe)
{
    assert(fe);
    assert(fe->fd);
    if (!fe->dataRefCount++)
    {
        size_t size;
        assert(!fe->data);
        if (feIsDirectory(fe))
        {
            FailIOErrno("Cannot read file", fe->path, EISDIR);
        }
        size = feSize(fe);
        if (!size)
        {
            fe->data = (byte*)"";
        }
        else
        {
            fe->data = (byte*)mmap(null, size, PROT_READ, MAP_PRIVATE, fe->fd, 0);
            if (fe->data == (byte*)-1)
            {
                fe->data = null;
                FailIO("Error reading file", fe->path);
            }
        }
    }
    assert(fe->data);
}

static void feMUnmap(FileEntry *fe)
{
    int error;

    assert(fe);
    assert(fe->data);
    assert(fe->dataRefCount);
    if (!--fe->dataRefCount)
    {
        size_t size = feSize(fe);
        if (size)
        {
            error = munmap(fe->data, size);
            assert(!error);
        }
        fe->data = null;
        feClose(fe);
    }
}


void FileInit(void)
{
    char *buffer;

    cwd = getcwd(null, 0);
    if (!cwd)
    {
        FailOOM();
    }
    cwdLength = strlen(cwd);
    assert(cwdLength);
    assert(cwd[0] == '/');
    if (cwd[cwdLength - 1] != '/')
    {
        buffer = (char*)realloc(cwd, cwdLength + 2);
        buffer[cwdLength] = '/';
        buffer[cwdLength + 1] = 0;
        cwd = buffer;
        cwdLength++;
    }
}

void FileDisposeAll(void)
{
    uint i;
    for (i = 0; i < sizeof(table) / sizeof(*table); i++)
    {
        if (table[i])
        {
            assert(table[i]->refCount == 1);
            clearTableEntry(i);
        }
    }
    free(cwd);
}


/* TODO: Handle backslashes */
static size_t cleanFilename(char *filename, size_t length)
{
    char *write = filename;
    const char *read = filename;
    const char *found = filename;

    assert(length);

    for (;;)
    {
        size_t l = (size_t)(filename + length - found);
        found = (char*)memchr((const void*)found, '/', l);
        if (!found)
        {
            l = (size_t)(filename + length - read);
            memmove(write, read, l + 1);
            length = (size_t)(write + l - filename);
            goto done;
        }
        found++;
        for (;;)
        {
            int skip;
            if (*found == '/')
            {
                /* Strip // */
                skip = 1;
            }
            else if (*found == '.')
            {
                if (found[1] == '/')
                {
                    /* Strip /./ */
                    skip = 2;
                }
                else if (!found[1])
                {
                    /* Strip /. */
                    l = (size_t)(found - read);
                    memmove(write, read, l);
                    write += l;
                    *write = 0;
                    length = (size_t)(write - filename);
                    goto done;
                }
                else if (found[1] == '.' && found[2] == '/' && !found[3])
                {
                    /* Strip /../ -> /.. or -> / at start of path*/
                    l = (size_t)(found - read);
                    memmove(write, read, l);
                    write += l;
                    if (write != filename + 1)
                    {
                        *write++ = '.';
                        *write++ = '.';
                    }
                    *write = 0;
                    length = (size_t)(write - filename);
                    goto done;
                }
                else if (found[1] == '.' && (found[2] == '/' || !found[2]) &&
                         write == filename + 1 && found == read)
                {
                    /* Strip /../ -> / at start of path */
                    skip = 2;
                }
                else
                {
                    break;
                }
            }
            else
            {
                break;
            }
            l = (size_t)(found - read);
            memmove(write, read, l);
            write += l;
            read = found + skip;
            found = read;
        }
    }

done:
    if (length >= 4 && filename[length - 4] == '/' && filename[length - 3] == '.' && filename[length - 2] == '.' && filename[length - 1] == '/')
    {
        filename[--length] = 0;
    }
    return length;
}

char *FileCreatePath(const char *restrict base, size_t baseLength,
                     const char *restrict path, size_t length,
                     const char *restrict extension, size_t extLength,
                     size_t *resultLength)
{
    const char *restrict base2 = null;
    size_t base2Length = 0;
    char *restrict buffer;
    size_t i;

    assert(!base || (baseLength && *base == '/'));
    assert(path);
    assert(length);
    assert(resultLength);

    if (path[0] == '/')
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
    *resultLength = cleanFilename(buffer, base2Length + baseLength + length + 1);

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

char *FileSearchPath(const char *name, size_t length, size_t *resultLength,
                     boolean executable)
{
    char *candidate;
    const char *path;
    size_t pathLength;
    const char *stop;
    const char *p;

    assert(name);
    assert(length);
    if (*name == '/')
    {
        return FileCreatePath(null, 0, name, length, null, 0, resultLength);
    }
    EnvGet("PATH", 4, &path, &pathLength);
    if (!path)
    {
        path = cwd;
        pathLength = cwdLength;
    }
    for (stop = path + pathLength; path < stop; path = p + 1)
    {
        for (p = path; p < stop && *p != ':'; p++);
        candidate = FileCreatePath(path, (size_t)(p - path), name, length,
                                   null, 0, resultLength);
        if (executable ? FileIsExecutable(candidate, *resultLength) :
            feIsFile(feEntry(candidate, *resultLength)))
        {
            return candidate;
        }
        free(candidate);
    }
    return null;
}

const char *FileStripPath(const char *path, size_t *length)
{
    const char *current;

    /* TODO: Relax some constraints? */
    assert(path);
    assert(length);
    assert(*length);
    assert(*path == '/');

    current = path + *length;
    while (current > path && current[-1] != '/')
    {
        current--;
    }
    *length = (size_t)(path + *length - current);
    return current;
}

void FileMarkModified(const char *path unused, size_t length unused)
{
    /* TODO: Do something clever. */
    uint i;
    for (i = 0; i < sizeof(table) / sizeof(*table); i++)
    {
        if (table[i])
        {
            clearTableEntry(i);
        }
    }
}

const FileStatus *FileGetStatus(const char *path, size_t length)
{
    FileEntry *fe = feEntry(path, length);
    feStat(fe);
    return &fe->status;
}

boolean FileHasChanged(const char *path, size_t length,
                       const FileStatus *status)
{
    return memcmp(FileGetStatus(path, length), status, sizeof(FileStatus)) != 0;
}

void FileOpen(File *file, const char *path, size_t length)
{
    if (!FileTryOpen(file, path, length))
    {
        FailIO("Error opening file", path);
    }
}

boolean FileTryOpen(File *file, const char *path, size_t length)
{
    FileEntry *fe;

    assert(file);
    assert(path);
    assert(length);
    assert(*path == '/');

    fe = feEntry(path, length);
    if (!fe->fd)
    {
        feOpen(fe, O_CLOEXEC | O_RDONLY);
        if (!fe->fd)
        {
            if (errno == ENOENT)
            {
                return false;
            }
            FailIO("Error opening file", fe->path);
        }
    }
    if (pathIsDirectory(path, length) && !feIsDirectory(fe))
    {
        FailIOErrno("Error opening directory", fe->path, ENOTDIR);
    }
    fe->refCount++;
    file->fe = fe;
    file->data = null;
    return true;
}

void FileOpenAppend(File *file, const char *path, size_t length, boolean truncate)
{
    FileEntry *fe;

    assert(file);
    assert(path);
    assert(length);
    assert(*path == '/');

    fe = feEntry(path, length);
    assert(fe->refCount == 1);
    assert(!fe->dataRefCount);
    if (fe->fd)
    {
        feClose(fe);
    }
    feOpen(fe, truncate ? O_CLOEXEC | O_CREAT | O_RDWR | O_APPEND | O_TRUNC :
           O_CLOEXEC | O_CREAT | O_RDWR | O_APPEND);
    if (!fe->fd)
    {
        FailIO("Error opening file", fe->path);
    }
    fe->refCount++;
    fe->hasStat = false;
    file->fe = fe;
    file->data = null;
}

void FileClose(File *file)
{
    assert(file);
    assert(file->fe);
    assert(file->fe->refCount);
    if (file->data)
    {
        feMUnmap(file->fe);
    }
    else if (!file->fe->dataRefCount)
    {
        feClose(file->fe);
    }
    if (!--file->fe->refCount)
    {
        free(file->fe);
    }
}

size_t FileSize(File *file)
{
    assert(file);
    assert(file->fe);
    return feSize(file->fe);
}

void FileRead(File *file, byte *buffer, size_t size)
{
    ssize_t sizeRead;

    assert(size);
    assert(size <= SSIZE_MAX);
    assert(file);
    assert(file->fe);
    assert(file->fe->fd);
    do
    {
        sizeRead = read(file->fe->fd, buffer, size);
        if (sizeRead < 0)
        {
            FailIO("Cannot read file", file->fe->path);
        }
        assert((size_t)sizeRead <= size);
        buffer += sizeRead;
        size -= size;
    }
    while (size);
}

void FileWrite(File *file, const byte *data, size_t size)
{
    assert(file);
    assert(file->fe);
    feWrite(file->fe, data, size);
}

boolean FileIsExecutable(const char *path, size_t length)
{
    FileEntry *fe = feEntry(path, length);
    feStat(fe);
    return feIsFile(fe) && (fe->mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

static void deleteDirectoryContents(const FileEntry *fe, int fd)
{
    int fd2;
    DIR *dir;
    struct dirent *d;

    assert(fd);
    fd2 = dup(fd);
    if (fd == -1)
    {
        FailIO("Error duplicating file handle", fe->path);
    }
    dir = fdopendir(fd2);
    if (!dir)
    {
        FailIO("Error opening directory", fe->path);
    }
    for (;;)
    {
        errno = 0;
        d = readdir(dir);
        if (!d)
        {
            if (errno)
            {
                FailIO("Error reading directory", fe->path);
            }
            break;
        }
        if (*d->d_name == '.' &&
            (!d->d_name[1] ||
             (d->d_name[1] == '.' && !d->d_name[2])))
        {
            continue;
        }
        /* TODO: Use d->d_type if available */
        if (unlinkat(fd, d->d_name, 0)) /* TODO: Provide fallback if unlinkat isn't available */
        {
            char *name = strdup(d->d_name);
            if (errno != EISDIR)
            {
                FailIO("Error deleting file", fe->path);
            }
            fd2 = openat(fd, d->d_name, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
            if (fd2 == -1)
            {
                FailIO("Error deleting directory", fe->path);
            }
            deleteDirectoryContents(fe, fd2); /* TODO: Iterate instead of recurse */
            close(fd2);
            if (unlinkat(fd, name, AT_REMOVEDIR))
            {
                FailIO("Error deleting directory", fe->path);
            }
            free(name);
        }
    }
    closedir(dir);
}

void FileDelete(const char *path, size_t length)
{
    FileEntry *fe = feEntry(path, length);
    uint i;

    assert(fe->refCount == 1); /* TODO */
    if (fe->hasStat && !fe->status.exists)
    {
        return;
    }
    if (!fe->hasStat || (fe->status.exists && !S_ISDIR(fe->mode)))
    {
        fe->hasStat = true;
        memset(&fe->status, 0, sizeof(fe->status));
        if (!unlink(fe->path) || errno == ENOENT)
        {
            return;
        }
        if (errno != EISDIR)
        {
            FailIO("Error deleting file", fe->path);
        }
    }

    for (i = 0; i < sizeof(table) / sizeof(*table); i++)
    {
        FileEntry *fe2 = table[i];
        if (fe2 && fe2->pathLength > length && !memcmp(fe2->path, path, length) &&
            fe2->path[length] == '/')
        {
            assert(fe2->refCount == 1); /* TODO */
            if (!fe2->hasStat || (fe2->status.exists && !S_ISDIR(fe2->mode)))
            {
                if (!unlink(fe2->path) && errno != ENOENT && errno != EISDIR)
                {
                    FailIO("Error deleting file", fe2->path);
                }
            }
            fe2->hasStat = true;
            memset(&fe2->status, 0, sizeof(fe2->status));
        }
    }

    if (!fe->fd)
    {
        feOpen(fe, O_CLOEXEC | O_RDONLY);
        if (!fe->fd)
        {
            FailIO("Error deleting directory", fe->path);
        }
    }
    deleteDirectoryContents(fe, fe->fd);
    feClose(fe);
    if (rmdir(fe->path))
    {
        FailIO("Error deleting directory", fe->path);
    }
}

boolean FileMkdir(const char *path, size_t length)
{
    FileEntry *fe = feEntry(path, length);
    if (((fe->hasStat && fe->status.exists) /*|| fe->fd*/))
    {
        if (S_ISDIR(fe->mode))
        {
            return false;
        }
        FailIOErrno("Cannot create directory", fe->path, EEXIST);
    }
    fe->hasStat = false;
    if (!mkdir(fe->path, S_IRWXU | S_IRWXG | S_IRWXO))
    {
        return true;
    }
    if (errno == ENOENT && length > 1)
    {
        FileMkdir(path, parentPathLength(path, length));
        fe = feEntry(path, length); /* TODO: Keep reference */
        if (!mkdir(fe->path, S_IRWXU | S_IRWXG | S_IRWXO))
        {
            return true;
        }
    }
    if (errno != EEXIST)
    {
        FailIO("Error creating directory", fe->path);
    }
    feStat(fe);
    if (fe->status.exists && S_ISDIR(fe->mode))
    {
        return false;
    }
    FailIOErrno("Cannot create directory", fe->path, EEXIST);
}

void FileCopy(const char *srcPath unused, size_t srcLength unused,
              const char *dstPath unused, size_t dstLength unused)
{
    FileEntry *feSrc = feEntry(srcPath, srcLength);
    FileEntry *feDst = feEntry(dstPath, dstLength);

    feOpen(feSrc, O_CLOEXEC | O_RDONLY);
    if (!feSrc->fd)
    {
        FailIO("Error opening file", feSrc->path);
    }
    assert(!feIsDirectory(feSrc)); /* TODO: Copy directory */
    feStat(feDst);
    if (feDst->status.exists && feSrc->ino == feDst->ino && feSrc->dev == feDst->dev)
    {
        return;
    }
    feMMap(feSrc);
    feOpen(feDst, O_CLOEXEC | O_CREAT | O_WRONLY | O_APPEND | O_TRUNC);
    if (!feDst->fd)
    {
        FailIO("Error opening file", feDst->path);
    }
    feWrite(feDst, feSrc->data, feSize(feSrc));
    feClose(feDst);
    feMUnmap(feSrc);
}

void FileRename(const char *oldPath, size_t oldLength,
                const char *newPath, size_t newLength)
{
    FileEntry *feOld = feEntry(oldPath, oldLength);
    FileEntry *feNew = feEntry(newPath, newLength);

    /* TODO */
    assert(!feOld->fd);
    assert(!feNew->fd);
    assert(feOld->refCount == 1);
    assert(feNew->refCount == 1);

    if (rename(feOld->path, feNew->path))
    {
        /* TODO: Rename directories and across file systems */
        Fail("Error renaming file from %s to %s: %s", feOld->path, feNew->path, strerror(errno));
    }
    feNew->hasStat = false; /* TODO: Move stat info? */
    feOld->hasStat = true;
    memset(&feOld->status, 0, sizeof(feOld->status));
}

void FileMMap(File *file, const byte **p, size_t *size)
{
    assert(file);
    assert(p);
    assert(size);
    assert(!file->data);

    feMMap(file->fe);
    file->data = file->fe->data;
    file->dataSize = file->fe->status.size;
    *p = file->data;
    *size = file->dataSize;
}

void FileMUnmap(File *file)
{
    assert(file);
    if (file->data)
    {
        feMUnmap(file->fe);
        file->data = null;
    }
}

static void traverseGlob(bytevector *path, int fdParent, size_t parentLength,
                         const char *pattern, size_t patternLength,
                         TraverseCallback callback, void *userdata)
{
    FileEntry *fe;
    const char *p;
    boolean asterisk;
    size_t componentLength;
    size_t childLength;
    size_t oldSize = BVSize(path);
    struct dirent *d;
    DIR *dir;
    int fd;
    int fd2;

    if (!patternLength)
    {
        return;
    }

    while (*pattern == '/')
    {
        pattern++;
        if (!--patternLength)
        {
            if (feIsDirectory(feEntry((const char*)BVGetPointer(path, 0), BVSize(path))))
            {
                callback((const char*)BVGetPointer(path, 0), BVSize(path), userdata);
            }
            return;
        }
    }

    asterisk = false;
    for (p = pattern; p < pattern + patternLength; p++)
    {
        if (*p == '/')
        {
            break;
        }
        else if (*p == '*')
        {
            asterisk = true;
        }
    }
    componentLength = (size_t)(p - pattern);
    if (!asterisk)
    {
        BVAddData(path, (const byte*)pattern, componentLength);
        if (p == pattern + patternLength &&
            feExists(feEntry((const char*)BVGetPointer(path, 0), BVSize(path))))
        {
            callback((const char*)BVGetPointer(path, 0), BVSize(path), userdata);
        }
        else
        {
            BVAdd(path, '/');
            traverseGlob(path, 0, oldSize, p, patternLength - componentLength, callback, userdata);
        }
        BVSetSize(path, oldSize);
        return;
    }

    if (!parentLength && oldSize > 1)
    {
        fd = open(".", O_CLOEXEC | O_RDONLY | O_DIRECTORY);
    }
    else
    {
        BVAdd(path, 0);
        if (fdParent)
        {
            fd = openat(fdParent, (const char*)BVGetPointer(path, parentLength),
                        O_CLOEXEC | O_RDONLY | O_DIRECTORY);
        }
        else
        {
            fd = open((const char*)BVGetPointer(path, 0), O_CLOEXEC | O_RDONLY | O_DIRECTORY);
        }
        BVPop(path);
    }
    if (fd == -1)
    {
        if (errno != ENOENT)
        {
            BVAdd(path, 0);
            FailIO("Error opening directory", (const char*)BVGetPointer(path, 0));
        }
        return;
    }
    fd2 = dup(fd);
    if (fd2 == -1)
    {
        BVAdd(path, 0);
        FailIO("Error duplicating file handle", (const char*)BVGetPointer(path, 0));
    }
    dir = fdopendir(fd2);
    if (!dir)
    {
        BVAdd(path, 0);
        FailIO("Error opening directory", (const char*)BVGetPointer(path, 0));
    }
    for (;;)
    {
        errno = 0;
        d = readdir(dir);
        if (!d)
        {
            if (errno)
            {
                BVAdd(path, 0);
                FailIO("Error reading directory", (const char*)BVGetPointer(path, 0));
            }
            break;
        }
        if (*d->d_name == '.' && *pattern != '.')
        {
            continue;
        }
        childLength = strlen(d->d_name);
        if (!GlobMatch(pattern, componentLength, d->d_name, childLength))
        {
            continue;
        }
        BVAddData(path, (const byte*)d->d_name, childLength);
        fe = feEntry((const char*)BVGetPointer(path, 0), BVSize(path));
        feStatat(fe, fd, d->d_name);
        if (p == pattern + patternLength)
        {
            callback((const char*)BVGetPointer(path, 0), BVSize(path), userdata);
        }
        else if (feIsDirectory(fe))
        {
            BVAdd(path, '/');
            traverseGlob(path, fd, oldSize, p, patternLength - componentLength, callback, userdata);
        }
        BVSetSize(path, oldSize);
    }
    closedir(dir);
    close(fd);
}

void FileTraverseGlob(const char *pattern, size_t length,
                      TraverseCallback callback, void *userdata)
{
    bytevector path;
    int fd;

    if (!length)
    {
        return;
    }

    assert(pattern);

    BVInit(&path, NAME_MAX);
    if (*pattern == '/')
    {
        BVAdd(&path, (byte)'/');
        fd = 0;
    }
    else
    {
        BVAddData(&path, (const byte*)cwd, cwdLength);
        fd = AT_FDCWD;
    }
    traverseGlob(&path, fd, 0, pattern, length, callback, userdata);
    BVDispose(&path);
}
