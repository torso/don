#include <stdlib.h>
#include <stdio.h>
#include "builder.h"
#include "fileindex.h"

#define INITIAL_FILE_SIZE 16

typedef struct
{
    byte* data;
    long length;
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
    fileIndex[fileCount].length = size;
    fileIndex[fileCount].data = malloc(size);
    assert(fileIndex[fileCount].data); /* TODO: handle oom */
    read = fread(fileIndex[fileCount].data, 1, size, f);
    assert(read == size); /* TODO: handle file error */
    fclose(f);
    return fileCount;
}
