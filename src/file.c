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
#include "file.h"
#include "glob.h"
#include "task.h"

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
    TreeEntry **children;
    size_t childCount;

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

static TreeEntry root;
static TreeEntry *teCwd;
static char *cwd;
static size_t cwdLength;


static boolean pathIsDirectory(const char *path, size_t length)
{
    return path[length] == '/';
}

static char *concatFilename(const TreeEntry *te) /* TODO: Experiment with cwd-relative paths. */
{
    size_t length = 0;
    const TreeEntry *parent = te;
    char *buffer;

    if (te == &root)
    {
        buffer = (char*)malloc(2);
        buffer[0] = '/';
        buffer[1] = 0;
        return buffer;
    }
    assert(te);
    for (parent = te; parent != &root; parent = parent->parent)
    {
        assert(parent);
        length += parent->componentLength + 1;
    }
    buffer = (char*)malloc(length + 1);
    *buffer = '/';
    buffer += length;
    *buffer = 0;
    for (parent = te; parent != &root; parent = parent->parent)
    {
        buffer -= parent->componentLength;
        memcpy(buffer, parent->component, parent->componentLength);
        *--buffer = '/';
    }
    return buffer;
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

static void teDoOpen(TreeEntry *te, int fdParent unused, int flags)
{
    char *path;
    assert(!te->refCount);
    assert(!te->fd);
    if (te == teCwd)
    {
        te->fd = open(".", flags,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
#ifdef HAVE_OPENAT
    else if (fdParent)
    {
        te->fd = openat(fdParent, te->component, flags,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
#endif
    else
    {
        path = concatFilename(te);
        te->fd = open(path, flags,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        free(path);
    }
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

#ifdef HAVE_OPENAT
static int teQuickParentFD(TreeEntry *restrict te)
{
    TreeEntry *restrict parent = te->parent;
    if (!parent)
    {
        return 0;
    }
    if (parent == teCwd)
    {
        return AT_FDCWD;
    }
    return te->parent->fd;
}

static void teOpenParent(TreeEntry *restrict te)
{
    TreeEntry *restrict parent = te->parent;
    if (!parent || parent->fd || parent == teCwd)
    {
        return;
    }
    teDoOpen(parent, teQuickParentFD(parent), O_CLOEXEC | O_RDONLY);
}

static int teParentFD(TreeEntry *restrict te)
{
    TreeEntry *restrict parent = te->parent;
    if (!parent)
    {
        return 0;
    }
    if (parent == teCwd)
    {
        return AT_FDCWD;
    }
    teOpenParent(te);
    return te->parent->fd;
}
#endif

static void teOpen(TreeEntry *te)
{
    assert(te);
    if (te->fd)
    {
        return;
    }

#ifdef HAVE_OPENAT
    teDoOpen(te, te == teCwd ? 0 : teParentFD(te), O_CLOEXEC | O_RDONLY);
#else
    teDoOpen(te, 0, O_CLOEXEC | O_RDONLY);
#endif
}

static void teOpenWrite(TreeEntry *te, int flags)
{
    assert(te);
    assert(!te->refCount);
    if (te->fd)
    {
        teClose(te);
    }

#ifdef HAVE_OPENAT
    teDoOpen(te, teParentFD(te), O_CLOEXEC | O_CREAT | O_WRONLY | flags);
#else
    teDoOpen(te, 0, O_CLOEXEC | O_CREAT | O_WRONLY | flags);
#endif
}

static DIR *teOpenDir(TreeEntry *te)
{
    DIR *dir;

    assert(te);
    if (!te->fd)
    {
#ifdef HAVE_OPENAT
        teDoOpen(te, te == teCwd ? 0 : teParentFD(te),
                 O_CLOEXEC | O_RDONLY | O_DIRECTORY);
#else
        teDoOpen(te, 0, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
#endif
        if (!te->fd)
        {
            TaskFailIO(concatFilename(te));
        }
    }
    dir = fdopendir(te->fd);
    if (!dir)
    {
        TaskFailIO(concatFilename(te));
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
        TaskFailIO(concatFilename(te));
    }
}

static void teStat(TreeEntry *te)
{
    struct stat s;
    struct stat s2;
    char *path;

    if (!te->hasStat)
    {
        if (te->fd)
        {
            if (fstat(te->fd, &s))
            {
                TaskFailIO(concatFilename(te));
            }
        }
        else
        {
#ifdef HAVE_OPENAT
            int fd = teParentFD(te);
            if (fd)
            {
                if (fstatat(fd, te->component, &s, 0))
                {
                    if (errno != ENOENT)
                    {
                        TaskFailIO(concatFilename(te));
                    }
                    if (fstatat(fd, te->component, &s, AT_SYMLINK_NOFOLLOW))
                    {
                        if (errno != ENOENT)
                        {
                            TaskFailIO(concatFilename(te));
                        }
                        te->hasStat = true;
                        te->blob.exists = false;
                        return;
                    }
                }
            }
            else
#endif
            {
                path = concatFilename(te);
                if (lstat(path, &s))
                {
                    if (errno != ENOENT)
                    {
                        TaskFailIO(concatFilename(te));
                    }
                    free(path);
                    te->hasStat = true;
                    te->blob.exists = false;
                    return;
                }
                if (S_ISLNK(s.st_mode))
                {
                    /* TODO: Maybe do something more clever with symlinks. */
                    if (stat(path, &s2))
                    {
                        if (errno != ENOENT)
                        {
                            TaskFailIO(path);
                        }
                    }
                    else
                    {
                        s = s2;
                    }
                }
                free(path);
            }
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
    char *path;
    DIR *dir;
    int fd;
    union
    {
        struct dirent d;
        char b[offsetof(struct dirent, d_name) + NAME_MAX + 1];
    } u;
    struct dirent *res;

    teOpen(te);
    if (!te->fd)
    {
        TaskFailIO(concatFilename(te));
    }
#ifndef HAVE_POSIX_SPAWN
#error TODO: dup clears O_CLOEXEC
#endif
    fd = dup(te->fd);
    if (fd == -1)
    {
        TaskFailIO(concatFilename(te));
    }
    dir = fdopendir(fd);
    if (!dir)
    {
        TaskFailIO(concatFilename(te));
    }
    u.d.d_name[NAME_MAX] = 0;
    for (;;)
    {
        if (readdir_r(dir, &u.d, &res))
        {
            TaskFailIO(concatFilename(te));
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
                TaskFailIO(concatFilename(&tempChild));
            }
            teDeleteDirectory(&tempChild); /* TODO: Iterate instead of recurse */
            assert(!tempChild.fd);
            assert(!tempChild.data);
            assert(!tempChild.refCount);
            assert(!tempChild.children);
        }
    }
    if (closedir(dir))
    {
        assert(false);
        TaskFailIO(concatFilename(te));
    }
    teClose(te);
#ifdef HAVE_OPENAT
    fd = teQuickParentFD(te);
    if (fd)
    {
        if (unlinkat(fd, te->component, AT_REMOVEDIR))
        {
            TaskFailIO(concatFilename(te));
        }
    }
    else
#endif
    {
        path = concatFilename(te);
        if (rmdir(path))
        {
            TaskFailIO(path);
        }
        free(path);
    }
}

static void teDelete(TreeEntry *te)
{
    TreeEntry **child;
#ifndef HAVE_OPENAT
    char *path;
#endif

    assert(te);
    assert(!te->refCount);

    if (te->hasStat && !te->blob.exists)
    {
        assert(!te->fd);
        return;
    }

#ifdef HAVE_OPENAT
    teOpenParent(te);
#endif
    if (te->fd)
    {
        teClose(te);
    }

    for (child = te->children; te->childCount; te->childCount--, child++)
    {
        teDelete(*child);
        free(*child);
    }
    free(te->children);
    te->children = null;

#ifdef HAVE_OPENAT
    if (!te->hasStat || !teIsDirectory(te))
    {
        if (!unlinkat(teParentFD(te), te->component, 0) ||
            errno == ENOENT)
        {
            te->hasStat = true;
            te->blob.exists = false;
            return;
        }
        if (errno != EISDIR)
        {
            TaskFailIO(concatFilename(te));
        }
    }
    if (!unlinkat(teParentFD(te), te->component, AT_REMOVEDIR) ||
        errno == ENOENT)
    {
        te->hasStat = true;
        te->blob.exists = false;
        return;
    }
    /* This might happen when deleting a symlink to a directory. */
    if (errno == ENOTDIR)
    {
        if (unlinkat(teParentFD(te), te->component, 0))
        {
            TaskFailIO(concatFilename(te));
        }
        te->hasStat = true;
        te->blob.exists = false;
        return;
    }
    if (errno != ENOTEMPTY)
    {
        TaskFailIO(concatFilename(te));
    }
    teDeleteDirectory(te);
    te->hasStat = true;
    te->blob.exists = false;
#else
    path = concatFilename(te);
    if (!te->hasStat || !teIsDirectory(te))
    {
        if (!unlink(path) || errno == ENOENT)
        {
            free(path);
            te->hasStat = true;
            te->blob.exists = false;
            return;
        }
        if (errno != EISDIR)
        {
            TaskFailIO(concatFilename(te));
        }
    }
    if (!rmdir(path) || errno == ENOENT)
    {
        free(path);
        te->hasStat = true;
        te->blob.exists = false;
        return;
    }
    /* This might happen when deleting a symlink to a directory. */
    if (errno == ENOTDIR)
    {
        if (unlink(path))
        {
            TaskFailIO(concatFilename(te));
        }
        te->hasStat = true;
        te->blob.exists = false;
        free(path);
        return;
    }
    if (errno != ENOTEMPTY)
    {
        TaskFailIO(concatFilename(te));
    }
    free(path);
    teDeleteDirectory(te);
    te->hasStat = true;
    te->blob.exists = false;
#endif
}

static void teMkdir(TreeEntry *te)
{
    char *name;

    if (((te->hasStat && te->blob.exists) || te->fd))
    {
        if (teIsDirectory(te))
        {
            return;
        }
        TaskFail("Cannot create directory, because a file already exists: %s\n",
                 concatFilename(te));
    }
    te->hasStat = false;
    name = concatFilename(te);
    if (!mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO)) /* TODO: Use mkdirat if available. */
    {
        free(name);
        return;
    }
    if (errno == ENOENT)
    {
        teMkdir(te->parent);
        if (!mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO)) /* TODO: Use mkdirat if available. */
        {
            free(name);
            return;
        }
    }
    if (errno != EEXIST)
    {
        TaskFailIO(name);
    }
    if (teIsDirectory(te))
    {
        free(name);
        return;
    }
    TaskFail("Cannot create directory, because a file already exists: %s\n", name);
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
            TaskFailIO(concatFilename(te));
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
            TaskFailIOErrno(EISDIR, concatFilename(te));
        }
        te->data = (byte*)mmap(null, teSize(te), PROT_READ, MAP_PRIVATE,
                               te->fd, 0);
        if (te->data == (byte*)-1)
        {
            te->data = null;
            TaskFailIO(concatFilename(te));
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

static uint teChildIndex(const TreeEntry *parent, const TreeEntry *child)
{
    uint i;
    assert(parent);
    assert(child);
    for (i = 0;; i++)
    {
        assert(i < parent->childCount);
        if (parent->children[i] == child)
        {
            return i;
        }
    }
}

static void teUnlinkChild(TreeEntry *parent, const TreeEntry *child)
{
    TreeEntry **newChildren;
    uint i;

    assert(parent);
    assert(child);
    assert(parent->childCount);
    if (parent->childCount == 1)
    {
        free(parent->children);
        parent->childCount = 0;
        parent->children = null;
        return;
    }
    newChildren = (TreeEntry**)malloc(
        sizeof(TreeEntry*) * (parent->childCount - 1));
    i = teChildIndex(parent, child);
    memcpy(newChildren, parent->children, i * sizeof(TreeEntry*));
    memcpy(newChildren + i, parent->children + i + 1,
           (parent->childCount - i - 1) * sizeof(TreeEntry*));
    free(parent->children);
    parent->children = newChildren;
    parent->childCount--;
}

static void teSetParent(TreeEntry *parent, TreeEntry *child)
{
    TreeEntry **newChildren;

    child->parent = parent;

    newChildren = (TreeEntry**)malloc(sizeof(TreeEntry*) * (parent->childCount + 1));
    memcpy(newChildren, parent->children, sizeof(TreeEntry*) * parent->childCount);
    newChildren[parent->childCount] = child;
    free(parent->children);
    parent->children = newChildren;
    parent->childCount++;
}

static TreeEntry *teChild(TreeEntry *te, const char *name, size_t length)
{
    TreeEntry *child;
    TreeEntry **childEntry;
    TreeEntry **stop;

    assert(te);
    assert(name);
    assert(length);
    assert(length > 1 || *name != '.');
    assert(length > 2 || *name != '.' || name[1] != '.');

    for (childEntry = te->children, stop = childEntry + te->childCount;
         childEntry < stop;
         childEntry++)
    {
        child = *childEntry;
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
    teSetParent(te, child);
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
    if (!te->childCount)
    {
        assert(!te->childCount);
        free(te->children);
        te->children = 0;
    }
    if (te->fd)
    {
        teClose(te);
    }
    te->hasStat = false;
}

static void teDisposeChildren(TreeEntry *te)
{
    TreeEntry *start = te;
    TreeEntry *parent;
    uint i;
    for (;;)
    {
        if (te->childCount && !te->children[te->childCount-1]->blockModify)
        {
            te = te->children[te->childCount-1];
        }
        else
        {
            for (;;)
            {
                if (te == start)
                {
                    return;
                }
                parent = te->parent;
                i = teChildIndex(parent, te);
                if (te->pinned || te->hasPinned)
                {
                    if (!te->pinned)
                    {
                        teDisposeContent(te);
                    }
                }
                else
                {
                    assert(!te->blockModify);
                    teDisposeContent(te);
                    free(te);
                    parent->childCount--;
                    parent->children[i] = parent->children[parent->childCount];
                }
                if (i)
                {
                    te = parent->children[i-1];
                    break;
                }
                else
                {
                    te = parent;
                }
            }
        }
    }
}

static void teDispose(TreeEntry *te)
{
    TreeEntry *parent = te->parent;

    if (te->pinned)
    {
        return;
    }
    teDisposeChildren(te);
    teDisposeContent(te);
    if (!te->hasPinned && parent)
    {
        teUnlinkChild(parent, te);
        free(te);
    }
}



void FileInit(void)
{
    char *buffer;

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
    teDispose(&root);
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
#ifdef HAVE_OPENAT
    int fd;
#endif

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
    fd = teQuickParentFD(te);
    teDoOpen(te, fd, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
    if (!te->fd)
    {
        if (errno != ENOENT)
        {
            TaskFailIO(path);
        }
        teMkdir(te);
        teDoOpen(te, fd, O_CLOEXEC | O_RDONLY | O_DIRECTORY);
        if (!te->fd)
        {
            TaskFailIO(path);
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
        TaskFailIO(path);
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
        TaskFailIO(path);
    }
    if (pathIsDirectory(path, length) && !teIsDirectory(te))
    {
        TaskFailIOErrno(ENOTDIR, path);
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
        TaskFailIO(path);
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
        TaskFailIO(concatFilename(teSrc));
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
        TaskFailIO(concatFilename(teDst));
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
    TreeEntry *teNewParent;
    char *oldPathSZ;
    char *newPathSZ;

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

    /* TODO: Avoid malloc if strings are null terminated. */
    oldPathSZ = concatFilename(teOld);
    newPathSZ = concatFilename(teNew);
    if (!rename(oldPathSZ, newPathSZ)) /* TODO: Use renameat if available. */
    {
        free(oldPathSZ);
        free(newPathSZ);
        teNewParent = teNew->parent;
        teDispose(teOld); /* TODO: Reparent */
        teDispose(teNew);
        return;
    }
    /* TODO: Rename directories and across file systems */
    TaskFailIO(oldPathSZ); /* TODO: Proper error message with both paths. */
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
                TaskFailIO(concatFilename(base));
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
    char *temp;

    if (!length)
    {
        return;
    }

    assert(pattern);

    if (*pattern == '/')
    {
        base = &root;
    }
    else
    {
        base = teGet(cwd, cwdLength);
    }

    BVInit(&path, NAME_MAX);
    temp = concatFilename(base); /* TODO: Avoid malloc */
    BVAddData(&path, (const byte*)temp, strlen(temp));
    free(temp);
    teTraverseGlob(base, &path, pattern, length, callback, userdata);
    BVDispose(&path);
}
