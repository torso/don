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

#define FILE_FREE_STRUCT 1
#define FILE_FREE_FILENAME 2

typedef struct
{
    boolean exists;
    size_t size;
    filetime_t mtime;
} StatusBlob;

struct _TreeEntry
{
    TreeEntry *parent;
    TreeEntry *firstChild;
    TreeEntry *sibling;

    char *component;
    size_t componentLength;
    uint refCount;

    int fd;
    uint8 pinned;
    uint8 hasPinned;
    uint8 blockModify;

    boolean hasStat;
    mode_t mode;
    dev_t dev;
    ino_t ino;

    byte *data;
    uint dataRefCount;

    StatusBlob blob;
};

static const char *rootPath = "/";
static const char *cwdRelative = ".";

static TreeEntry root;
static TreeEntry *teCwd;
static char *cwd;
static size_t cwdLength;


static boolean pathIsDirectory(const char *path, size_t length)
{
    return path[length] == '/';
}

static void writePathSegment(const TreeEntry *te, const TreeEntry *teStop,
                             char *buffer, size_t length)
{
    assert(te != teStop);
    buffer += length;
    *buffer = 0;
    buffer -= te->componentLength;
    memcpy(buffer, te->component, te->componentLength);
    for (te = te->parent; te != teStop; te = te->parent)
    {
        *--buffer = '/';
        buffer -= te->componentLength;
        memcpy(buffer, te->component, te->componentLength);
    }
}

/*
  Returns an absolute path, or a path relative to CWD.
  The returned path must be freed with freePath.
*/
static const char *cwdRelativePath(const TreeEntry *te)
{
    const TreeEntry *parent;
    size_t length = 0;
    char *buffer;

    if (te == &root)
    {
        return rootPath;
    }
    if (te == teCwd)
    {
        return cwdRelative;
    }
    if (te->parent == teCwd)
    {
        return te->component;
    }
    assert(te);
    for (parent = te; parent != &root && parent != teCwd; parent = parent->parent)
    {
        assert(parent);
        length += parent->componentLength + 1;
    }
    if (parent == &root)
    {
        buffer = (char*)malloc(length + 1);
        *buffer = '/';
        writePathSegment(te, &root, buffer + 1, length - 1);
    }
    else
    {
        buffer = (char*)malloc(length);
        writePathSegment(te, parent, buffer, length - 1);
    }
    return buffer;
}

/*
  The returned path must be freed with freePath.
*/
static const char *relativePath(const TreeEntry *te, int *fd)
{
    const TreeEntry *parent;
    size_t length = 0;
    char *buffer;

    *fd = AT_FDCWD;
    if (te == &root)
    {
        return rootPath;
    }
    if (te == teCwd)
    {
        return cwdRelative;
    }
    if (te->parent == teCwd)
    {
        return te->component;
    }
    assert(te);
    assert(!te->fd);
    for (parent = te; parent != &root && parent != teCwd; parent = parent->parent)
    {
        assert(parent);
#ifdef HAVE_OPENAT
        if (parent->fd)
        {
            *fd = parent->fd;
            break;
        }
#endif
        length += parent->componentLength + 1;
    }
    if (parent == &root)
    {
        buffer = (char*)malloc(length + 1);
        *buffer = '/';
        writePathSegment(te, &root, buffer + 1, length - 1);
    }
    else
    {
        buffer = (char*)malloc(length);
        writePathSegment(te, parent, buffer, length - 1);
    }
    return buffer;
}

static void freePath(const TreeEntry *te, const char *path)
{
    if (path != te->component && path != rootPath && path != cwdRelative)
    {
        free((char*)path);
    }
}


static void teClose(TreeEntry *te)
{
    assert(te);
    assert(te->fd);
    assert(!te->refCount);
    assert(!te->data);

    close(te->fd);
    te->fd = 0;
}

static void teDoOpen(TreeEntry *te, int flags)
{
    int fd;
    const char *path;
    assert(!te->refCount);
    assert(!te->fd);
    path = relativePath(te, &fd);
#ifdef HAVE_OPENAT
    te->fd = openat(fd, path, flags,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
#else
    te->fd = open(path, flags,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
#endif
    freePath(te, path);
    if (te->fd == -1)
    {
        te->fd = 0;
        if (errno == ENOENT)
        {
            assert(!te->hasStat || !te->blob.exists || (flags & O_CREAT));
            te->hasStat = true;
            te->blob.exists = false;
        }
        return;
    }
    assert(!te->hasStat || te->blob.exists || (flags & O_CREAT));
}

static void teOpen(TreeEntry *te)
{
    assert(te);
    if (te->fd)
    {
        return;
    }

    teDoOpen(te, O_CLOEXEC | O_RDONLY);
}

static void teOpenWrite(TreeEntry *te, int flags)
{
    assert(te);
    assert(!te->refCount);
    if (te->fd)
    {
        teClose(te);
    }

    teDoOpen(te, O_CLOEXEC | O_CREAT | O_WRONLY | flags);
}

static DIR *teOpenDir(TreeEntry *te)
{
    DIR *dir;

    assert(te);
    if (!te->fd)
    {
        teDoOpen(te, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
        if (!te->fd)
        {
            FailIO("Error opening directory", cwdRelativePath(te));
        }
    }
    dir = fdopendir(te->fd);
    if (!dir)
    {
        FailIO("Error opening directory", cwdRelativePath(te));
    }
    return dir;
}

static void teCloseDir(TreeEntry *te, DIR *dir)
{
    assert(te);
    assert(dir);
    assert(te->fd);
    assert(!te->refCount);
    te->fd = 0;
    if (closedir(dir))
    {
        assert(false);
        FailIO("Error closing directory", cwdRelativePath(te));
    }
}

static void teStat(TreeEntry *te)
{
    struct stat s;
    struct stat s2;

    if (!te->hasStat)
    {
        if (te->fd)
        {
            if (fstat(te->fd, &s))
            {
                FailIO("Error accessing file", cwdRelativePath(te));
            }
        }
        else
        {
            int fd;
            const char *path = relativePath(te, &fd);
            if (
#ifdef HAVE_OPENAT
                fstatat(fd, path, &s, AT_SYMLINK_NOFOLLOW)
#else
                lstat(path, &s)
#endif
                )
            {
                if (errno != ENOENT)
                {
                    FailIO("Error accessing file", cwdRelativePath(te));
                }
                freePath(te, path);
                te->hasStat = true;
                te->blob.exists = false;
                return;
            }
            if (S_ISLNK(s.st_mode))
            {
                /* TODO: Maybe do something more clever with symlinks. */
                fflush(stdout);
                if (
#ifdef HAVE_OPENAT
                    fstatat(fd, path, &s2, 0)
#else
                    stat(path, &s2)
#endif
                    )
                {
                    if (errno != ENOENT)
                    {
                        FailIO("Error accessing file", cwdRelativePath(te));
                    }
                }
                else
                {
                    s = s2;
                }
            }
            freePath(te, path);
        }
        te->hasStat = true;
        te->blob.exists = true;
        te->mode = s.st_mode;
        te->dev = s.st_dev;
        te->ino = s.st_ino;
        te->blob.size = (size_t)s.st_size;
        te->blob.mtime.seconds = s.st_mtime;
        te->blob.mtime.fraction = (ulong)s.st_mtim.tv_nsec;
    }
}

static boolean teExists(TreeEntry *te)
{
    teStat(te);
    return te->blob.exists;
}

static boolean teIsFile(TreeEntry *te)
{
    return teExists(te) && S_ISREG(te->mode);
}

static boolean teIsDirectory(TreeEntry *te)
{
    return teExists(te) && S_ISDIR(te->mode);
}

static boolean teIsExecutable(TreeEntry *te)
{
    return teIsFile(te) && (te->mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

static size_t teSize(TreeEntry *te)
{
    teStat(te);
    return te->blob.size;
}

static void teDeleteDirectory(TreeEntry *te)
{
    TreeEntry tempChild;
    DIR *dir;
    int fd;
    union
    {
        struct dirent d;
        char b[offsetof(struct dirent, d_name) + NAME_MAX + 1];
    } u;
    struct dirent *res;
    const char *path;

    teOpen(te);
    if (!te->fd)
    {
        FailIO("Error opening directory", cwdRelativePath(te));
    }
#ifndef HAVE_POSIX_SPAWN
#error TODO: dup clears O_CLOEXEC
#endif
    fd = dup(te->fd);
    if (fd == -1)
    {
        FailIO("Error duplicating file handle", cwdRelativePath(te));
    }
    dir = fdopendir(fd);
    if (!dir)
    {
        FailIO("Error opening directory", cwdRelativePath(te));
    }
    u.d.d_name[NAME_MAX] = 0;
    for (;;)
    {
        if (readdir_r(dir, &u.d, &res))
        {
            FailIO("Error reading directory", cwdRelativePath(te));
        }
        if (!res)
        {
            break;
        }
        if (*u.d.d_name == '.' &&
            (!u.d.d_name[1] ||
             (u.d.d_name[1] == '.' && !u.d.d_name[2])))
        {
            continue;
        }
        /* TODO: Use u.d.d_type if available */
        if (unlinkat(fd, u.d.d_name, 0)) /* TODO: Provide fallback if unlinkat isn't available */
        {
            memset(&tempChild, 0, sizeof(tempChild));
            tempChild.parent = te;
            tempChild.component = u.d.d_name;
            tempChild.componentLength = strlen(u.d.d_name);
            if (errno != EISDIR)
            {
                FailIO("Error deleting file", cwdRelativePath(&tempChild));
            }
            teDeleteDirectory(&tempChild); /* TODO: Iterate instead of recurse */
            assert(!tempChild.fd);
            assert(!tempChild.data);
            assert(!tempChild.refCount);
            assert(!tempChild.firstChild);
        }
    }
    if (closedir(dir))
    {
        assert(false);
        FailIO("Error closing directory", cwdRelativePath(te));
    }
    teClose(te);
    path = relativePath(te, &fd);
    if (
#ifdef HAVE_OPENAT
        unlinkat(fd, path, AT_REMOVEDIR)
#else
        rmdir(path)
#endif
        )
    {
        FailIO("Error deleting directory", cwdRelativePath(te));
    }
    freePath(te, path);
}

static void teDelete(TreeEntry *te)
{
    TreeEntry *child;
    const char *path;
    int fd;

    assert(te);
    assert(!te->refCount);

    if (te->hasStat && !te->blob.exists)
    {
        assert(!te->fd);
        return;
    }

    if (te->fd)
    {
        teClose(te);
    }

    for (child = te->firstChild; child;)
    {
        TreeEntry *next = child->sibling;
        teDelete(child);
        free(child);
        child = next;
    }
    te->firstChild = null;

    path = relativePath(te, &fd);
    if (!te->hasStat || !teIsDirectory(te))
    {
        if (!(
#ifdef HAVE_OPENAT
                unlinkat(fd, path, 0)
#else
                unlink(path)
#endif
                ) || errno == ENOENT)
        {
            freePath(te, path);
            te->hasStat = true;
            te->blob.exists = false;
            return;
        }
        if (errno != EISDIR)
        {
            FailIO("Error deleting file", cwdRelativePath(te));
        }
    }
    if (!(
#ifdef HAVE_OPENAT
            unlinkat(fd, path, AT_REMOVEDIR)
#else
            rmdir(path)
#endif
            ) || errno == ENOENT)
    {
        freePath(te, path);
        te->hasStat = true;
        te->blob.exists = false;
        return;
    }
    /* This might happen when deleting a symlink to a directory. */
    if (errno == ENOTDIR)
    {
        if (
#ifdef HAVE_OPENAT
            unlinkat(fd, path, 0)
#else
            unlink(path)
#endif
            )
        {
            FailIO("Error deleting file", cwdRelativePath(te));
        }
        freePath(te, path);
        te->hasStat = true;
        te->blob.exists = false;
        return;
    }
    if (errno != ENOTEMPTY)
    {
        FailIO("Error deleting directory", cwdRelativePath(te));
    }
    freePath(te, path);
    teDeleteDirectory(te);
    te->hasStat = true;
    te->blob.exists = false;
}

static void teMkdir(TreeEntry *te)
{
    const char *path;
    int fd;

    if (((te->hasStat && te->blob.exists) || te->fd))
    {
        if (teIsDirectory(te))
        {
            return;
        }
        FailIOErrno("Cannot create directory", cwdRelativePath(te), EEXIST);
    }
    te->hasStat = false;
    path = relativePath(te, &fd);
    if (!(
#ifdef HAVE_OPENAT
            mkdirat(fd, path, S_IRWXU | S_IRWXG | S_IRWXO)
#else
            mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO)
#endif
            ))
    {
        freePath(te, path);
        return;
    }
    if (errno == ENOENT)
    {
        teMkdir(te->parent);
        if (!(
#ifdef HAVE_OPENAT
                mkdirat(fd, path, S_IRWXU | S_IRWXG | S_IRWXO)
#else
                mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO)
#endif
                ))
        {
            freePath(te, path);
            return;
        }
    }
    if (errno != EEXIST)
    {
        FailIO("Error creating directory", cwdRelativePath(te));
    }
    freePath(te, path);
    if (teIsDirectory(te))
    {
        return;
    }
    FailIOErrno("Cannot create directory", cwdRelativePath(te), EEXIST);
}

static void teWrite(TreeEntry *te, const byte *data, size_t size)
{
    ssize_t written;

    assert(te->fd);
    while (size)
    {
        written = write(te->fd, data, size);
        if (written < 0)
        {
            FailIO("Error writing to file", cwdRelativePath(te));
        }
        assert((size_t)written <= size);
        size -= (size_t)written;
        data += written;
    }
}

static void teMMap(TreeEntry *te)
{
    assert(te);
    assert(te->fd);
    if (!te->dataRefCount++)
    {
        assert(!te->data);
        if (teIsDirectory(te))
        {
            FailIOErrno("Cannot read file", cwdRelativePath(te), EISDIR);
        }
        te->data = (byte*)mmap(null, teSize(te), PROT_READ, MAP_PRIVATE,
                               te->fd, 0);
        if (te->data == (byte*)-1)
        {
            te->data = null;
            FailIO("Error reading file", cwdRelativePath(te));
        }
    }
    assert(te->data);
}

static void teMUnmap(TreeEntry *te)
{
    int error;

    assert(te);
    assert(te->data);
    assert(te->dataRefCount);
    if (!--te->dataRefCount)
    {
        error = munmap(te->data, teSize(te));
        te->data = null;
        assert(!error);
    }
}

static void tePin(TreeEntry *te, boolean blockModify, int delta)
{
    assert(te);
    te->pinned = (uint8)(te->pinned + delta);
    if (blockModify)
    {
        te->blockModify = (uint8)(te->blockModify + delta);
    }
    assert(!te->blockModify || te->pinned);
    for (;;)
    {
        te = te->parent;
        if (!te)
        {
            break;
        }
        te->hasPinned = (uint8)(te->hasPinned + delta);
    }
}

static TreeEntry *teChild(TreeEntry *te, const char *name, size_t length)
{
    TreeEntry *child;

    assert(te);
    assert(name);
    assert(length);
    assert(length > 1 || *name != '.');
    assert(length > 2 || *name != '.' || name[1] != '.');

    for (child = te->firstChild; child; child = child->sibling)
    {
        if (child->componentLength == length &&
            !memcmp(child->component, name, length))
        {
            return child;
        }
    }

    child = (TreeEntry*)calloc(sizeof(*child) + length + 1, 1);
    child->component = (char*)child + sizeof(*child);
    child->componentLength = length;
    memcpy(child->component, name, length);

    child->parent = te;
    child->sibling = te->firstChild;
    te->firstChild = child;
    return child;
}

static TreeEntry *teGet(const char *name, size_t length)
{
    const char *component = name;
    const char *slash;
    size_t componentLength;
    TreeEntry *te = &root;

    assert(name);
    assert(length);
    assert(*name == '/');

    if (length == 1)
    {
        return te;
    }

    component++;
    length--;
    for (;;)
    {
        slash = (const char*)memchr(component, '/', length);
        assert(slash != component);
        if (!slash)
        {
            return teChild(te, component, length);
        }
        if (!slash[1])
        {
            return teChild(te, component, length - 1);
        }
        componentLength = (size_t)(slash - component);
        te = teChild(te, component, componentLength);
        component = slash + 1;
        length -= componentLength + 1;
    }
}

static void teDisposeContent(TreeEntry *te)
{
    assert(!te->pinned);
    assert(!te->refCount);
    if (te->fd)
    {
        teClose(te);
    }
    te->hasStat = false;
}

static void teDisposeChildren(TreeEntry *te)
{
    TreeEntry *start = te;
    TreeEntry **prev;

    /* This does a post-order traversal. */
    for (;;)
    {
        if (!te->blockModify && te->firstChild)
        {
            te = te->firstChild;
        }
        else if (te == start)
        {
            return;
        }
        else if (te->sibling)
        {
            te = te->sibling;
        }
        else
        {
            /* All children have been visited. Dispose on the way up. */
            do
            {
                te = te->parent;
                for (prev = &te->firstChild; *prev;)
                {
                    TreeEntry *child = *prev;
                    if (!child->pinned)
                    {
                        teDisposeContent(child);
                    }
                    if (child->pinned || child->hasPinned)
                    {
                        prev = &child->sibling;
                    }
                    else
                    {
                        *prev = child->sibling;
                        free(child);
                    }
                }
                if (te == start)
                {
                    return;
                }
            }
            while (!te->sibling);
            te = te->sibling;
        }
    }
}

static void teDispose(TreeEntry *te)
{
    if (te->pinned)
    {
        return;
    }
    teDisposeChildren(te);
    teDisposeContent(te);
    if (!te->hasPinned)
    {
        TreeEntry **prev;
        assert(te->parent);
        for (prev = &te->parent->firstChild; *prev != te;
             prev = &(*prev)->sibling);
        *prev = te->sibling;
        free(te);
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
    teCwd = teGet(cwd, cwdLength);
    tePin(teCwd, false, 1);
}

void FileDisposeAll(void)
{
    tePin(teGet(cwd, cwdLength), false, -1);
    teDisposeChildren(&root);
    teDisposeContent(&root);
    free(cwd);
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
            teIsFile(teGet(candidate, *resultLength)))
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

void FilePinDirectory(const char *path, size_t length)
{
    TreeEntry *te;

    assert(path);
    assert(length);
    assert(*path == '/');

    te = teGet(path, length);

    /* This function should only be called at startup. */
    assert(!te->refCount);
    assert(!te->fd);
    assert(te != teCwd); /* TODO: Ensure cache directory isn't a parent of cwd. */

#ifdef HAVE_OPENAT
    assert(te->parent);
    teDoOpen(te, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
    if (!te->fd)
    {
        if (errno != ENOENT)
        {
            FailIO("Error opening directory", path);
        }
        teMkdir(te);
        teDoOpen(te, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
        if (!te->fd)
        {
            FailIO("Error opening directory", path);
        }
    }
#else
    teMkdir(te);
#endif

    tePin(te, true, 1);
}

void FileUnpinDirectory(const char *path, size_t length)
{
    TreeEntry *te;

    assert(path);
    assert(length);
    assert(*path == '/');

    te = teGet(path, length);
    tePin(te, true, -1);
}

void FileMarkModified(const char *path, size_t length)
{
    assert(path);
    assert(length);
    assert(*path == '/');
    teDispose(teGet(path, length));
}

const byte *FileStatusBlob(const char *path, size_t length)
{
    TreeEntry *te = teGet(path, length);
    teStat(te);
    return (const byte*)&te->blob;
}

size_t FileStatusBlobSize(void)
{
    return sizeof(StatusBlob);
}

boolean FileHasChanged(const char *path, size_t length, const byte *blob)
{
    return memcmp(FileStatusBlob(path, length), blob,
                  FileStatusBlobSize()) != 0;
}

boolean FileIsOpen(File *file)
{
    assert(file);
    assert(!file->te || file->te->refCount);
    assert(!file->te || file->te->fd);
    return file->te != null;
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
    TreeEntry *te;

    assert(file);
    assert(path);
    assert(length);
    assert(*path == '/');

    te = teGet(path, length);
    teOpen(te);
    if (!te->fd)
    {
        if (errno == ENOENT)
        {
            return false;
        }
        FailIO("Error opening file", path);
    }
    if (pathIsDirectory(path, length) && !teIsDirectory(te))
    {
        FailIOErrno("Error opening file", path, ENOTDIR);
    }
    te->refCount++;
    file->te = te;
    file->mmapRefCount = 0;
    return true;
}

void FileOpenAppend(File *file, const char *path, size_t length)
{
    TreeEntry *te;

    assert(file);
    assert(path);
    assert(length);
    assert(*path == '/');

    te = teGet(path, length);
    assert(!te->fd); /* TODO: Reopen file for appending. */
    teOpenWrite(te, O_APPEND);
    if (!te->fd)
    {
        FailIO("Error opening file", path);
    }
    te->refCount++;
    file->te = te;
    file->mmapRefCount = 0;
}

void FileClose(File *file)
{
    assert(FileIsOpen(file));

    if (file->mmapRefCount)
    {
        teMUnmap(file->te);
    }
    if (!--file->te->refCount)
    {
        teClose(file->te);
    }
    file->te = null;
}

size_t FileSize(File *file)
{
    assert(file);
    assert(file->te);
    return teSize(file->te);
}

void FileRead(File *file, byte *buffer, size_t size)
{
    assert(file);
    assert(file->te);
    teMMap(file->te);
    assert(teSize(file->te) >= size);
    memcpy(buffer, file->te->data, size);
    teMUnmap(file->te);
}

void FileWrite(File *file, const byte *data, size_t size)
{
    assert(file);
    assert(file->te);
    teWrite(file->te, data, size);
}

boolean FileIsExecutable(const char *path, size_t length)
{
    assert(path);
    assert(length);
    assert(*path == '/');
    return teIsExecutable(teGet(path, length));
}

void FileDelete(const char *path, size_t length)
{
    assert(path);
    assert(length);
    assert(*path == '/');
    teDelete(teGet(path, length));
}

void FileMkdir(const char *path, size_t length)
{
    assert(path);
    assert(length);
    assert(*path == '/');
    teMkdir(teGet(path, length));
}

void FileCopy(const char *srcPath, size_t srcLength,
              const char *dstPath, size_t dstLength)
{
    TreeEntry *teSrc;
    TreeEntry *teDst;

    assert(srcPath);
    assert(srcLength);
    assert(*srcPath == '/');
    assert(dstPath);
    assert(dstLength);
    assert(*dstPath == '/');

    teSrc = teGet(srcPath, srcLength);
    teDst = teGet(dstPath, dstLength);
    teOpen(teSrc);
    if (!teSrc->fd)
    {
        FailIO("Error opening file", cwdRelativePath(teSrc));
    }
    assert(!teIsDirectory(teSrc)); /* TODO: Copy directory */
    teStat(teDst);
    if (teDst->blob.exists &&
        teSrc->ino == teDst->ino && teSrc->dev == teDst->dev)
    {
        return;
    }
    teMMap(teSrc);
    teOpenWrite(teDst, O_TRUNC);
    if (!teDst->fd)
    {
        FailIO("Error opening file", cwdRelativePath(teDst));
    }
    teWrite(teDst, teSrc->data, teSize(teSrc));
    teClose(teDst);
    teMUnmap(teSrc);
}

void FileRename(const char *oldPath, size_t oldLength,
                const char *newPath, size_t newLength)
{
    TreeEntry *teOld;
    TreeEntry *teNew;
    const char *oldPathSZ;
    const char *newPathSZ;
    int fdOld;
    int fdNew;

    assert(oldPath);
    assert(oldLength);
    assert(*oldPath == '/');
    assert(newPath);
    assert(newLength);
    assert(*newPath == '/');

    teOld = teGet(oldPath, oldLength);
    teNew = teGet(newPath, newLength);

    /* TODO: Error handling? */
    assert(!teOld->pinned);
    assert(!teOld->hasPinned);
    assert(!teNew->pinned);
    assert(!teNew->hasPinned);

    oldPathSZ = relativePath(teOld, &fdOld);
    newPathSZ = relativePath(teNew, &fdNew);
    if (
#ifdef HAVE_OPENAT
        renameat(fdOld, oldPathSZ, fdNew, newPathSZ)
#else
        rename(oldPathSZ, newPathSZ)
#endif
        )
    {
        /* TODO: Rename directories and across file systems */
        Fail("Error renaming file from %s to %s: %s",
             cwdRelativePath(teOld), cwdRelativePath(teNew), strerror(errno));
    }
    freePath(teOld, oldPathSZ);
    freePath(teNew, newPathSZ);
    teDispose(teOld); /* TODO: Reparent */
    teDispose(teNew);
}

void FileMMap(File *file, const byte **p, size_t *size)
{
    assert(file);
    assert(p);
    assert(size);
    assert(file->te);

    if (!file->mmapRefCount)
    {
        teMMap(file->te);
    }
    file->mmapRefCount++;
    *p = file->te->data;
    *size = teSize(file->te);
}

void FileMUnmap(File *file)
{
    assert(file);
    assert(file->te);
    assert(file->te->data);
    assert(file->mmapRefCount);

    if (!--file->mmapRefCount)
    {
        teMUnmap(file->te);
    }
}

static void teCallback(TreeEntry *te, bytevector *path,
                       const char *component, size_t componentLength,
                       TraverseCallback callback, void *userdata)
{
    size_t oldSize = BVSize(path);
    if (componentLength)
    {
        if (te->parent != &root)
        {
            BVAdd(path, '/');
        }
        BVAddData(path, (const byte*)component, componentLength);
    }
    if (teIsDirectory(te))
    {
        BVAdd(path, '/');
    }
    callback((const char*)BVGetPointer(path, 0), BVSize(path), userdata);
    BVSetSize(path, oldSize);
}

static void teTraverseGlob(TreeEntry *base, bytevector *path,
                           const char *pattern, size_t patternLength,
                           TraverseCallback callback, void *userdata)
{
    TreeEntry *child;
    const char *p;
    const char *stop;
    boolean asterisk;
    size_t componentLength;
    size_t childLength;
    size_t oldSize;
    DIR *dir;
    union
    {
        struct dirent d;
        char b[offsetof(struct dirent, d_name) + NAME_MAX + 1];
    } u;
    struct dirent *res;

    for (;;)
    {
        if (!patternLength)
        {
            return;
        }

        while (*pattern == '/')
        {
            pattern++;
            if (!--patternLength)
            {
                teCallback(base, path, null, 0, callback, userdata);
                return;
            }
        }

        asterisk = false;
        for (p = pattern, stop = pattern + patternLength; p < stop; p++)
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
            base = teChild(base, pattern, componentLength);
            if (p == stop && teExists(base))
            {
                teCallback(base, path, pattern, componentLength,
                           callback, userdata);
            }
            if (teIsDirectory(base))
            {
                if (base->parent != &root)
                {
                    BVAdd(path, '/');
                }
                BVAddData(path, (const byte*)pattern, componentLength);
                pattern = p;
                patternLength -= componentLength;
                continue;
            }
            return;
        }

        dir = teOpenDir(base);
        u.d.d_name[NAME_MAX] = 0;
        for (;;)
        {
            if (readdir_r(dir, &u.d, &res))
            {
                FailIO("Error reading directory", cwdRelativePath(base));
            }
            if (!res)
            {
                break;
            }
            if (*u.d.d_name == '.' && *pattern != '.')
            {
                continue;
            }
            childLength = strlen(u.d.d_name);
            if (!GlobMatch(pattern, componentLength, u.d.d_name, childLength))
            {
                continue;
            }
            child = teChild(base, u.d.d_name, childLength);
            if (p == stop)
            {
                teCallback(child, path, u.d.d_name, childLength,
                           callback, userdata);
            }
            else if (teIsDirectory(child))
            {
                oldSize = BVSize(path);
                if (base != &root)
                {
                    BVAdd(path, '/');
                }
                BVAddData(path, (const byte*)u.d.d_name, childLength);
                teTraverseGlob(child, path, p, patternLength - componentLength,
                               callback, userdata);
                BVSetSize(path, oldSize);
            }
        }
        teCloseDir(base, dir);
        break;
    }
}

void FileTraverseGlob(const char *pattern, size_t length,
                      TraverseCallback callback, void *userdata)
{
    bytevector path;
    TreeEntry *base;

    if (!length)
    {
        return;
    }

    assert(pattern);

    BVInit(&path, NAME_MAX);
    if (*pattern == '/')
    {
        base = &root;
        BVAdd(&path, (byte)'/');
    }
    else
    {
        base = teGet(cwd, cwdLength);
        BVAddData(&path, (const byte*)cwd, cwdLength - 1);
    }
    teTraverseGlob(base, &path, pattern, length, callback, userdata);
    BVDispose(&path);
}
