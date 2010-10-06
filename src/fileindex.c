#include <ftw.h>
#include <memory.h>
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
}

fileref FileIndexOpen(const char *filename)
{
    uint file;
    FILE *f;
    int status;
    long l;
    size_t size;
    size_t read;
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
    read = fread(data, 1, size, f);
    assert(read == size); /* TODO: handle file error */
    fclose(f);
    fileIndex[file].flags = 0;
    fileIndex[file].name = filename;
    fileIndex[file].data = data;
    fileIndex[file].size = size;
    fileIndex[file].refCount = 1;
    return file + 1;
}

fileref FileIndexAdd(const char *filename)
{
    uint file;
    void *filenameBuffer;
    size_t length;

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

    length = strlen(filename);
    filenameBuffer = malloc(length + 1);
    if (!filenameBuffer)
    {
        return 0;
    }
    memcpy(filenameBuffer, filename, length + 1);
    fileIndex[file].flags = FLAG_FREE_FILENAME;
    fileIndex[file].name = (const char*)filenameBuffer;
    fileIndex[file].data = null;
    fileIndex[file].size = 0;
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

    if (!GlobMatch(globalPattern, filename))
    {
        return 0;
    }
    file = FileIndexAdd(filename);
    if (!file)
    {
        return OUT_OF_MEMORY;
    }
    return globalCallback(file, globalUserdata);
}

ErrorCode FileIndexTraverseGlob(const char *pattern,
                                TraverseCallback callback, void *userdata)
{
    int error;

    globalCallback = callback;
    globalUserdata = userdata;
    globalPattern = pattern;
    error = ftw("test/", globTraverse, 10);
    if (error < 0)
    {
        /* TODO: Error handling. */
        return OUT_OF_MEMORY;
    }
    return error;
}
