#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"
#include "bytevector.h"
#include "env.h"
#include "fail.h"
#include "file.h"
#include "glob.h"
#include "hash.h"

struct _FileEntry
{
    FileStatus status;
    int timeStamp;
    size_t pathLength;
    char *path;
    char pathBuffer[72+128];
};

static FileEntry table[0x400];
static const uint tableMask = sizeof(table) / sizeof(*table) - 1;
static char *cwd;
static size_t cwdLength;
static int currentTimeStamp;


static char *dupPath(const char *path, size_t length)
{
    char *pathZ = (char*)malloc(length + 1);
    memcpy(pathZ, path, length);
    pathZ[length] = 0;
    return pathZ;
}

static bool pathIsDirectory(const char *path, size_t length)
{
    return path[length] == '/';
}

static size_t parentPathLength(const char *path, size_t length)
{
    assert(length > 1);
    assert(*path == '/');
    length--;
    if (path[length] == '/')
    {
        length--;
    }
    while (path[length] != '/')
    {
        length--;
    }
    return length + 1;
}


static uint feIndex(const char *path, size_t length)
{
    assert(path);
    assert(length);
    assert(*path == '/');
    return HashString(path, length) & tableMask;
}

static void clearTableEntry(uint index)
{
    if (table[index].path != table[index].pathBuffer)
    {
        free(table[index].path);
    }
    table[index].path = null;
    table[index].pathLength = 0;
}

static bool feIsEntry(uint index, const char *path, size_t length)
{
    FileEntry *fe = table + index;

    return fe->timeStamp == currentTimeStamp &&
        fe->pathLength == length && !memcmp(fe->path, path, length);
}

static FileEntry *feEntryAt(const char *path, size_t length, int fdParent, const char *relPath)
{
    uint index = feIndex(path, length);
    FileEntry *fe = table + index;
    struct stat s;
    struct stat s2;

    if (fe->timeStamp == currentTimeStamp &&
        fe->pathLength == length && !memcmp(fe->path, path, length))
    {
        return fe;
    }

    fe->timeStamp = currentTimeStamp;
    fe->pathLength = length;
    fe->path = length >= sizeof(fe->pathBuffer) ? (char*)malloc(length + 1) : fe->pathBuffer;
    memcpy(fe->path, path, length);
    fe->path[length] = 0;
    if (!relPath)
    {
        relPath = fe->path;
    }

    memset(&fe->status, 0, sizeof(fe->status));
    fe->status.size = -1;
    if (
#ifdef HAVE_OPENAT
        fstatat(fdParent, relPath, &s, AT_SYMLINK_NOFOLLOW)
#else
        lstat(fe->path, &s)
#endif
        )
    {
        if (unlikely(errno != ENOENT))
        {
            FailIO("Error accessing file", fe->path);
        }
        return fe;
    }
    if (S_ISLNK(s.st_mode))
    {
        /* TODO: Maybe do something more clever with symlinks. */
        if (
#ifdef HAVE_OPENAT
            fstatat(fdParent, relPath, &s2, 0)
#else
            stat(fe->path, &s2)
#endif
            )
        {
            if (unlikely(errno != ENOENT))
            {
                FailIO("Error accessing file", fe->path);
            }
        }
        else
        {
            s = s2;
        }
    }

    fe->status.mode = s.st_mode;
    fe->status.ino = s.st_ino;
    fe->status.uid = s.st_uid;
    fe->status.gid = s.st_gid;
    fe->status.size = s.st_size;
    fe->status.mtime = s.st_mtime;
    return fe;
}

static FileEntry *feEntry(const char *path, size_t length)
{
    return feEntryAt(path, length, AT_FDCWD, null);
}

static bool feExists(FileEntry *fe)
{
    return fe->status.size >= 0;
}

static bool feIsFile(FileEntry *fe)
{
    return feExists(fe) && S_ISREG(fe->status.mode);
}

static bool feIsDirectory(FileEntry *fe)
{
    return feExists(fe) && S_ISDIR(fe->status.mode);
}


void FileInit(void)
{
    char *buffer;

    cwd = getcwd(null, 0);
    if (unlikely(!cwd))
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
        clearTableEntry(i);
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
                     bool executable)
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
    currentTimeStamp++;
}

const FileStatus *FileGetStatus(const char *path, size_t length)
{
    return &feEntry(path, length)->status;
}

bool FileHasChanged(const char *path, size_t length,
                    const FileStatus *status)
{
    return memcmp(FileGetStatus(path, length), status, sizeof(FileStatus)) != 0;
}

void FileOpen(File *file, const char *path, size_t length)
{
    if (unlikely(!FileTryOpen(file, path, length)))
    {
        FailIO("Error opening file", path);
    }
}

bool FileTryOpen(File *file, const char *path, size_t length)
{
    int fd;

    assert(file);
    assert(path);
    assert(length);
    assert(*path == '/');
    assert(!pathIsDirectory(path, length));

    fd = open(path, O_CLOEXEC | O_RDONLY);
    if (fd == -1)
    {
        if (likely(errno == ENOENT))
        {
            return false;
        }
        FailIO("Error opening file", path);
    }
    file->fd = fd;
    file->path = path;
    file->data = null;
    return true;
}

void FileOpenAppend(File *file, const char *path, size_t length, bool truncate)
{
    int fd;

    assert(file);
    assert(path);
    assert(length);
    assert(*path == '/');
    assert(!pathIsDirectory(path, length));

    /* TODO: Update file table */
    fd = open(path, truncate ? O_CLOEXEC | O_CREAT | O_RDWR | O_APPEND | O_TRUNC :
              O_CLOEXEC | O_CREAT | O_RDWR | O_APPEND,
              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (unlikely(fd < 0))
    {
        FailIO("Error opening file", path);
    }
    file->fd = fd;
    file->data = null;
}

void FileClose(File *file)
{
    assert(file);
    assert(file->fd);
    assert(!file->data);
    close(file->fd);
}

size_t FileSize(File *file)
{
    struct stat s;

    assert(file);
    assert(file->fd);
    if (unlikely(fstat(file->fd, &s)))
    {
        FailIO("Error accessing file", file->path);
    }
    return (size_t)s.st_size;
}

void FileRead(File *file, byte *buffer, size_t size)
{
    assert(size);
    assert(size <= SSIZE_MAX);
    assert(file);
    assert(file->fd);
    do
    {
        ssize_t sizeRead = read(file->fd, buffer, size);
        if (unlikely(sizeRead < 0))
        {
            FailIO("Cannot read file", file->path);
        }
        assert((size_t)sizeRead <= size);
        buffer += sizeRead;
        size -= (size_t)sizeRead;
    }
    while (size);
}

void FileWrite(File *file, const byte *data, size_t size)
{
    assert(file);
    assert(file->fd);
    while (size)
    {
        ssize_t written = write(file->fd, data, size);
        if (unlikely(written < 0))
        {
            FailIO("Error writing to file", file->path);
        }
        assert((size_t)written <= size);
        size -= (size_t)written;
        data += written;
    }
}

bool FileIsExecutable(const char *path, size_t length)
{
    FileEntry *fe = feEntry(path, length);
    return feIsFile(fe) && (fe->status.mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

static void deleteDirectoryContents(const char *path, int fd)
{
    int fd2;
    DIR *dir;
    struct dirent *d;

    assert(fd);
    fd2 = dup(fd);
    if (unlikely(fd < 0))
    {
        FailIO("Error duplicating file handle", path);
    }
    dir = fdopendir(fd2);
    if (unlikely(!dir))
    {
        FailIO("Error opening directory", path);
    }
    for (;;)
    {
        errno = 0;
        d = readdir(dir);
        if (!d)
        {
            if (unlikely(errno))
            {
                FailIO("Error reading directory", path);
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
            if (unlikely(errno != EISDIR))
            {
                FailIO("Error deleting file", path);
            }
            fd2 = openat(fd, d->d_name, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
            if (unlikely(fd2 < 0))
            {
                FailIO("Error deleting directory", path);
            }
            deleteDirectoryContents(path, fd2); /* TODO: Iterate instead of recurse */
            close(fd2);
            if (unlikely(unlinkat(fd, name, AT_REMOVEDIR)))
            {
                FailIO("Error deleting directory", path);
            }
            free(name);
        }
    }
    closedir(dir);
}

void FileDelete(const char *path, size_t length)
{
    uint index = feIndex(path, length);
    char *pathZ;
    int fd;

    if (feIsEntry(index, path, length))
    {
        FileEntry *fe = table + index;
        if (!feExists(fe))
        {
            return;
        }
    }
    pathZ = dupPath(path, length);
    FileMarkModified(pathZ, length);
    if (!unlink(pathZ) || errno == ENOENT)
    {
        free(pathZ);
        return;
    }
    if (unlikely(errno != EISDIR))
    {
        FailIO("Error deleting file", pathZ);
    }

    fd = open(pathZ, O_CLOEXEC | O_RDONLY);
    if (unlikely(fd < 0))
    {
        FailIO("Error deleting directory", pathZ);
    }
    deleteDirectoryContents(pathZ, fd);
    close(fd);
    if (unlikely(rmdir(pathZ)))
    {
        FailIO("Error deleting directory", pathZ);
    }
    free(pathZ);
}

bool FileMkdir(const char *path, size_t length)
{
    uint index = feIndex(path, length);
    char *pathZ;
    if (feIsEntry(index, path, length))
    {
        FileEntry *fe = table + index;
        if (feExists(fe))
        {
            if (likely(S_ISDIR(fe->status.mode)))
            {
                return false;
            }
            FailIOErrno("Cannot create directory", fe->path, EEXIST);
        }
    }
    /* TODO: Avoid malloc and copy */
    pathZ = dupPath(path, length);
    FileMarkModified(pathZ, length);
    if (!mkdir(pathZ, S_IRWXU | S_IRWXG | S_IRWXO))
    {
        free(pathZ);
        return true;
    }
    if (errno == ENOENT && length > 1)
    {
        FileMkdir(path, parentPathLength(path, length));
        if (!mkdir(pathZ, S_IRWXU | S_IRWXG | S_IRWXO))
        {
            free(pathZ);
            return true;
        }
    }
    if (unlikely(errno != EEXIST))
    {
        FailIO("Error creating directory", pathZ);
    }
    free(pathZ);
    return false;
}

void FileCopy(const char *srcPath, size_t srcLength unused,
              const char *dstPath, size_t dstLength unused)
{
    int srcfd;
    int dstfd;
    struct stat srcStat;
    struct stat dstStat;

    srcfd = open(srcPath, O_CLOEXEC | O_RDONLY);
    if (unlikely(srcfd == -1))
    {
        FailIO("Error opening file", srcPath);
    }
    if (unlikely(fstat(srcfd, &srcStat)))
    {
        FailIO("Error accessing file", srcPath);
    }
    assert(!S_ISDIR(srcStat.st_mode)); /* TODO: Copy directory */

    FileMarkModified(dstPath, dstLength);
    dstfd = open(dstPath, O_CLOEXEC | O_CREAT | O_WRONLY,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (unlikely(dstfd == -1))
    {
        FailIO("Error opening file", dstPath);
    }
    if (unlikely(fstat(dstfd, &dstStat)))
    {
        FailIO("Error accessing file", dstPath);
    }
    if (srcStat.st_ino != dstStat.st_ino || srcStat.st_dev != dstStat.st_dev)
    {
        byte buffer[4096];
        if (unlikely(ftruncate(dstfd, 0)))
        {
            FailIO("Error truncating file", dstPath);
        }
        for (;;)
        {
            ssize_t r = read(srcfd, buffer, sizeof(buffer));
            if (r <= 0)
            {
                if (unlikely(r))
                {
                    FailIO("Error reading file", srcPath);
                }
                break;
            }
            if (unlikely(write(dstfd, buffer, (size_t)r) != r))
            {
                FailIO("Error writing file", dstPath);
            }
        }
    }

    close(srcfd);
    close(dstfd);
}

void FileRename(const char *oldPath, size_t oldLength,
                const char *newPath, size_t newLength)
{
    char *oldPathZ = dupPath(oldPath, oldLength);
    char *newPathZ = dupPath(newPath, newLength);

    if (unlikely(rename(oldPathZ, newPathZ)))
    {
        /* TODO: Rename directories and across file systems */
        Fail("Error renaming file from %s to %s: %s", oldPathZ, newPathZ, strerror(errno));
    }
    FileMarkModified(oldPathZ, oldLength);
    FileMarkModified(newPathZ, newLength);
    free(oldPathZ);
    free(newPathZ);
}

void FileMMap(File *file, const byte **p, size_t *size)
{
    size_t s;

    assert(p);
    assert(size);
    assert(file);
    assert(file->fd);
    assert(!file->data);

    s = FileSize(file);
    file->data = (byte*)mmap(null, s, PROT_READ, MAP_PRIVATE, file->fd, 0);
    if (unlikely(file->data == (byte*)-1))
    {
        FailIO("Error reading file", file->path);
    }
    file->dataSize = s;
    *p = file->data;
    *size = s;
}

void FileMUnmap(File *file)
{
    assert(file);
    assert(file->data);

    munmap(file->data, file->dataSize);
    file->data = null;
}

static void traverseGlob(bytevector *path, int fdParent, size_t parentLength,
                         const char *pattern, size_t patternLength,
                         TraverseCallback callback, void *userdata)
{
    FileEntry *fe;
    const char *p;
    bool asterisk;
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
        if (unlikely(errno != ENOENT))
        {
            BVAdd(path, 0);
            FailIO("Error opening directory", (const char*)BVGetPointer(path, 0));
        }
        return;
    }
    fd2 = dup(fd);
    if (unlikely(fd2 == -1))
    {
        BVAdd(path, 0);
        FailIO("Error duplicating file handle", (const char*)BVGetPointer(path, 0));
    }
    dir = fdopendir(fd2);
    if (unlikely(!dir))
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
            if (unlikely(errno))
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
        fe = feEntryAt((const char*)BVGetPointer(path, 0), BVSize(path), fd, d->d_name);
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
