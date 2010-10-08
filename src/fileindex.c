#include <ftw.h>
#include <memory.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include "common.h"
#include "glob.h"
#include "stringpool.h"
#include "fileindex.h"

#define INITIAL_FILE_SIZE 128

#define FLAG_FREE_FILENAME 1

typedef struct
{
    const char *name;
    byte *data;
    size_t size;
    int flags;
    uint refCount;
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
    assert(file <= sizeof(fileIndex) / sizeof(fileIndex[0]));
    assert(fileIndex[file - 1].refCount);
}

static FileEntry *getFile(fileref file)
{
    checkFile(file);
    return &fileIndex[file - 1];
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


ErrorCode FileIndexInit(void)
{
    cwd = getcwd(null, 0);
    if (!cwd)
    {
        return OUT_OF_MEMORY;
    }
    cwdLength = strlen(cwd);
    return NO_ERROR;
}

void FileIndexDispose(void)
{
    uint i = sizeof(fileIndex) / sizeof(fileIndex[0]);
    while (--i)
    {
        if (fileIndex[i].refCount)
        {
            fileIndex[i].refCount = 1;
            FileIndexClose(i + 1);
        }
    }
    free(cwd);
}


static fileref addFile(const char *filename, boolean filenameOwner)
{
    uint file;

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

    fileIndex[file].flags = filenameOwner ? FLAG_FREE_FILENAME : 0;
    fileIndex[file].name = (const char*)filename;
    fileIndex[file].data = null;
    fileIndex[file].size = 0;
    fileIndex[file].refCount = 1;
    return file + 1;
}

fileref FileIndexAdd(const char *filename, size_t length)
{
    return addFile(getAbsoluteFilename(null, 0, filename, length), true);
}

fileref FileIndexOpen(const char *filename)
{
    uint file;
    FILE *f;
    int status;
    long l;
    size_t size;
    size_t bytes;
    byte *data;

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

    f = fopen(filename, "rb");
    assert(f); /* TODO: handle file error */
    setvbuf(f, null, _IONBF, 0);
    status = fseek(f, 0, SEEK_END);
    assert(!status); /* TODO: handle file error */
    l = ftell(f);
    assert(l != -1); /* TODO: handle file error */
    size = (size_t)l;
    status = fseek(f, 0, SEEK_SET);
    assert(!status); /* TODO: handle file error */
    data = (byte*)malloc(size + 1);
    assert(data); /* TODO: handle oom */
    data[size] = 0;
    bytes = fread(data, 1, size, f);
    assert(bytes == size); /* TODO: handle file error */
    fclose(f);
    fileIndex[file].flags = 0;
    fileIndex[file].name = filename;
    fileIndex[file].data = data;
    fileIndex[file].size = size;
    fileIndex[file].refCount = 1;
    return file + 1;
}

void FileIndexClose(fileref file)
{
    FileEntry *fe = getFile(file);
    if (!--fe->refCount)
    {
        if (fe->flags & FLAG_FREE_FILENAME)
        {
            free((void*)fe->name);
        }
        free(fe->data);
    }
}

const char *FileIndexGetName(fileref file)
{
    return getFile(file)->name;
}

const byte *FileIndexGetContents(fileref file)
{
    return getFile(file)->data;
}

size_t FileIndexGetSize(fileref file)
{
    return getFile(file)->size;
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
    file = addFile(copyString(filename, length), true);
    if (!file)
    {
        return OUT_OF_MEMORY;
    }
    return globalCallback(file, globalUserdata);
}

const char *FileIndexFilename(const char *path, size_t *length)
{
    const char *current = path + *length;
    while (current > path && current[-1] != '/')
    {
        current--;
    }
    *length = (size_t)(path + *length - current);
    return current;
}

ErrorCode FileIndexTraverseGlob(const char *pattern,
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
        file = FileIndexAdd(pattern, length);
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
