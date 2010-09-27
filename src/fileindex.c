#include <stdio.h>
#include "builder.h"
#include "stringpool.h"
#include "fileindex.h"

#define INITIAL_FILE_SIZE 16

typedef struct
{
    const char *name;
    byte *data;
    size_t size;
    uint refCount;
} FileEntry;

static FileEntry fileIndex[16];

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
