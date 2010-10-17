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

#define INITIAL_FILE_SIZE 128

#define FLAG_FREE_FILENAME 1

typedef struct
{
    const char *name;
    size_t nameLength;
    int flags;
    uint refCount;

    int fd;
    byte *data;
    size_t size;
} FileEntry;

static FileEntry fileIndex[INITIAL_FILE_SIZE];

static TraverseCallback globalCallback;
static void *globalUserdata;
static const char *globalPattern;
static size_t globalFilenamePrefixLength;
static char *cwd;
static size_t cwdLength;


static void checkFile(fileref file)
{
    assert(file);
    assert(sizeFromRef(file) <= sizeof(fileIndex) / sizeof(fileIndex[0]));
    assert(fileIndex[sizeFromRef(file) - 1].refCount);
}

static FileEntry *getFile(fileref file)
{
    checkFile(file);
    return &fileIndex[sizeFromRef(file) - 1];
}

static char *copyString(const char *restrict string, size_t length)
{
    char *restrict buffer = (char*)malloc(length + 1);
    if (!buffer)
    {
        return null;
    }
    memcpy(buffer, string, length);
    buffer[length] = 0;
    return buffer;
}

static char *cleanFilename(char *filename, size_t length)
{
    char *p;

    /* TODO: Strip /../ */
    for (p = filename + length; p != filename; p--)
    {
        if (*p == '/')
        {
            if (!p[1] || p[1] == '/')
            {
                /* Strip // */
                memmove(p, p + 1, length - (size_t)(p - filename));
                length--;
            }
            else if (p[1] == '.' && (!p[2] || p[2] == '/'))
            {
                /* Strip /./ */
                memmove(p, p + 2, length - (size_t)(p - filename) - 1);
                length -= 2;
            }
        }
    }
    return filename;
}

static char *getAbsoluteFilename(const char *restrict base, size_t baseLength,
                                 const char *restrict path, size_t length)
{
    char *restrict buffer;

    if (path[0] == '/')
    {
        return cleanFilename(copyString(path, length), length);
    }
    if (!base)
    {
        base = cwd;
        baseLength = cwdLength;
    }

    assert(base[0] == '/');
    if (!length || (length == 1 && path[0] == '.'))
    {
        return cleanFilename(copyString(base, baseLength), baseLength);;
    }
    assert(path[0] != '/');
    buffer = (char*)malloc(baseLength + length + 2);
    if (!buffer)
    {
        return null;
    }
    memcpy(buffer, base, baseLength);
    buffer[baseLength] = '/';
    memcpy(&buffer[baseLength + 1], path, length);
    buffer[baseLength + length + 1] = 0;
    return cleanFilename(buffer, baseLength + length + 1);
}

static fileref addFile(const char *filename, size_t filenameLength,
                       boolean filenameOwner)
{
    size_t file;

    if (!filename)
    {
        return 0;
    }

    file = sizeof(fileIndex) / sizeof(fileIndex[0]);
    for (;;)
    {
        assert(file); /* TODO: grow file index */
        file--;
        if (!fileIndex[file].refCount)
        {
            break;
        }
    }

    fileIndex[file].name = (const char*)filename;
    fileIndex[file].nameLength = filenameLength;
    fileIndex[file].flags = filenameOwner ? FLAG_FREE_FILENAME : 0;
    fileIndex[file].refCount = 1;
    fileIndex[file].fd = 0;
    fileIndex[file].data = null;
    return refFromSize(file + 1);
}

static ErrorCode openFile(FileEntry *fe)
{
    struct stat s;

    if (fe->fd)
    {
        return NO_ERROR;
    }
    fe->fd = open(fe->name, O_RDONLY);
    if (fe->fd <= 0)
    {
        return ERROR_IO;
    }
    if (fstat(fe->fd, &s))
    {
        return ERROR_IO;
    }
    fe->size = (size_t)s.st_size;
    return NO_ERROR;
}


ErrorCode FileInit(void)
{
    cwd = getcwd(null, 0);
    if (!cwd)
    {
        return OUT_OF_MEMORY;
    }
    cwdLength = strlen(cwd);
    return NO_ERROR;
}

void FileDisposeAll(void)
{
    size_t i = sizeof(fileIndex) / sizeof(fileIndex[0]);
    while (--i)
    {
        if (fileIndex[i].refCount)
        {
            fileIndex[i].refCount = 1;
            FileDispose(refFromSize(i + 1));
        }
    }
    free(cwd);
}


fileref FileAdd(const char *filename, size_t length)
{
    filename = getAbsoluteFilename(null, 0, filename, length);
    return addFile(filename, strlen(filename), true);
}

fileref FileAddRelative(const char *base, size_t baseLength,
                        const char *filename, size_t length)
{
    filename = getAbsoluteFilename(base, baseLength, filename, length);
    return addFile(filename, strlen(filename), true);
}

void FileDispose(fileref file)
{
    FileEntry *fe = getFile(file);
    if (fe->refCount == 1)
    {
        if (fe->flags & FLAG_FREE_FILENAME)
        {
            free((void*)fe->name);
        }
        if (fe->data)
        {
            FileMUnmap(file);
        }
    }
    fe->refCount--;
}


const char *FileGetName(fileref file)
{
    return getFile(file)->name;
}

size_t FileGetNameLength(fileref file)
{
    return getFile(file)->nameLength;
}


ErrorCode FileMMap(fileref file, const byte **p, size_t *size)
{
    FileEntry *fe = getFile(file);
    ErrorCode error;

    if (!fe->data)
    {
        error = openFile(fe);
        if (error)
        {
            return error;
        }
        fe->data = (byte*)mmap(null, fe->size, PROT_READ, MAP_PRIVATE, fe->fd, 0);
        if (!fe->data)
        {
            /* TODO: Read file fallback */
            return ERROR_IO;
        }
    }
    *p = fe->data;
    *size = fe->size;
    return NO_ERROR;
}

ErrorCode FileMUnmap(fileref file)
{
    FileEntry *fe = getFile(file);

    assert(fe->data);
    return munmap(fe->data, fe->size) ? ERROR_IO : NO_ERROR; /* TODO: Error handling */
}


static int globTraverse(const char *filename, const struct stat *info unused,
                        int flags unused)
{
    fileref file;
    size_t length = strlen(filename);

    if (length < globalFilenamePrefixLength)
    {
        return 0;
    }
    if (!GlobMatch(globalPattern, filename + globalFilenamePrefixLength))
    {
        return 0;
    }
    file = addFile(copyString(filename, length), length, true);
    if (!file)
    {
        return OUT_OF_MEMORY;
    }
    return globalCallback(file, globalUserdata);
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

ErrorCode FileTraverseGlob(const char *pattern,
                                TraverseCallback callback, void *userdata)
{
    const char *slash = null;
    const char *asterisk = null;
    const char *p;
    char *filename;
    size_t length;
    fileref file;
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
        file = FileAdd(pattern, length);
        if (!file)
        {
            return OUT_OF_MEMORY;
        }
        return callback(file, userdata);
    }

    if (slash)
    {
        filename = getAbsoluteFilename(null, 0, pattern, (size_t)(slash - pattern));
    }
    else
    {
        filename = cwd;
    }
    if (!filename)
    {
        return OUT_OF_MEMORY;
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
    if (error < 0)
    {
        /* TODO: Error handling. */
        return OUT_OF_MEMORY;
    }
    return error;
}
