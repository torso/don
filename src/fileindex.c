#include <stdlib.h>
#include <stdio.h>
#include "builder.h"
#include "fileindex.h"

#define INITIAL_FILE_SIZE 16

typedef struct
{
    const byte* data;
    ulong length;
} FileEntry;

static FileEntry fileIndex[16];
static uint fileCount = 0;

void FileIndexInit()
{
}

fileref FileIndexAdd(const char* filename)
{
    FILE* f;
    int status;
    long size;
    long read;
    byte* data;

    fileCount++;
    assert(fileCount < INITIAL_FILE_SIZE); /* TODO: grow file index */

    f = fopen(filename, "rb");
    assert(f); /* TODO: handle file error */
    setvbuf(f, null, _IONBF, 0);
    status = fseek(f, 0, SEEK_END);
    assert(!status); /* TODO: handle file error */
    size = ftell(f);
    assert(size >= 0); /* TODO: handle file error */
    status = fseek(f, 0, SEEK_SET);
    assert(!status); /* TODO: handle file error */
    data = malloc(size + 1);
    assert(data); /* TODO: handle oom */
    data[size] = 0;
    read = fread(data, 1, size, f);
    assert(read == size); /* TODO: handle file error */
    fclose(f);
    fileIndex[fileCount].length = size;
    fileIndex[fileCount].data = data;
    return fileCount;
}

const byte* FileIndexGetContents(fileref file)
{
    assert(file >= 1 && file <= fileCount);
    return fileIndex[file].data;
}

ulong FileIndexGetSize(fileref file)
{
    assert(file >= 1 && file <= fileCount);
    return fileIndex[file].length;
}
