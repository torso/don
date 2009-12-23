#include <stdlib.h>
#include <stdio.h>
#include "builder.h"
#include "stringpool.h"
#include "fileindex.h"

#define INITIAL_FILE_SIZE 16

typedef struct
{
    const byte *data;
    stringref name;
    uint length;
} FileEntry;

static FileEntry fileIndex[16];
static uint fileCount = 0;

void FileIndexFree(void)
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
    uint size;
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
    assert(l >= 0); /* TODO: handle file error */
    assert(l <= MAX_UINT); /* TODO: handle large files */
    size = (uint)l;
    status = fseek(f, 0, SEEK_SET);
    assert(!status); /* TODO: handle file error */
    data = (byte*)malloc(size + 1);
    assert(data); /* TODO: handle oom */
    data[size] = 0;
    read = fread(data, 1, size, f);
    assert(read == size); /* TODO: handle file error */
    fclose(f);
    fileIndex[fileCount].length = size;
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

uint FileIndexGetSize(fileref file)
{
    assert(file >= 1 && file <= fileCount);
    return fileIndex[file].length;
}
