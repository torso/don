#include <stdio.h>
#include "builder.h"
#include "stringpool.h"
#include "fileindex.h"

#define INITIAL_FILE_SIZE 16

typedef struct
{
    const byte *data;
    stringref name;
    size_t size;
} FileEntry;

static FileEntry fileIndex[16];
static uint fileCount = 0;

void FileIndexDispose(void)
{
    while (fileCount > 0)
    {
        assert(fileIndex[fileCount].data);
        free((byte*)fileIndex[fileCount].data);
        fileCount--;
    }
}

fileref FileIndexAdd(const char *filename)
{
    FILE *f;
    int status;
    long l;
    size_t size;
    size_t read;
    byte *data;

    fileCount++;
    assert(fileCount < INITIAL_FILE_SIZE); /* TODO: grow file index */

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
    fileIndex[fileCount].size = size;
    fileIndex[fileCount].data = data;
    fileIndex[fileCount].name = StringPoolAdd(filename);
    return fileCount;
}

stringref FileIndexGetName(fileref file)
{
    assert(file >= 1 && file <= fileCount);
    return fileIndex[file].name;
}

const byte *FileIndexGetContents(fileref file)
{
    assert(file >= 1 && file <= fileCount);
    return fileIndex[file].data;
}

size_t FileIndexGetSize(fileref file)
{
    assert(file >= 1 && file <= fileCount);
    return fileIndex[file].size;
}
