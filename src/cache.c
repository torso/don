#include "memory.h"
#include "common.h"
#include "bytevector.h"
#include "cache.h"
#include "file.h"
#include "hash.h"
#include "util.h"

#define FILENAME_DIGEST_SIZE (DIGEST_SIZE - (DIGEST_SIZE % 5))

typedef struct
{
    byte hash[FILENAME_DIGEST_SIZE];
    fileref file;
    boolean uptodate;
    boolean newEntry;
    bytevector dependencies;
} Entry;

static fileref cacheDir;
static fileref cacheIndex;
static fileref cacheIndexTemp;
static Entry entries[16];


static Entry *getEntry(cacheref ref)
{
    assert(entries[sizeFromRef(ref)].file);
    return &entries[sizeFromRef(ref)];
}


ErrorCode CacheInit(void)
{
    cacheDir = FileAdd(".don/cache", 10);
    cacheIndex = FileAdd(".don/cache/index", 16);
    cacheIndexTemp = FileAdd(".don/cache/index.tmp", 20);
    if (!cacheDir || !cacheIndex || !cacheIndexTemp)
    {
        return OUT_OF_MEMORY;
    }
    return FileMkdir(cacheDir);
}

void CacheDispose(void)
{
    Entry *entry;
    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (entry->file)
        {
            ByteVectorDispose(&entry->dependencies);
        }
    }
}

ErrorCode CacheGet(const byte *hash, cacheref *ref)
{
    Entry *entry;
    Entry *freeEntry = null;
    char filename[FILENAME_DIGEST_SIZE / 5 * 8 + 1];
    ErrorCode error;

    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (!entry->file)
        {
            freeEntry = entry;
        }
        else
        {
            if (!memcmp(hash, entry->hash, FILENAME_DIGEST_SIZE))
            {
                *ref = refFromSize((size_t)(entry - entries));
                return NO_ERROR;
            }
        }
    }

    if (!freeEntry)
    {
        return OUT_OF_MEMORY;
    }

    UtilBase32(hash, FILENAME_DIGEST_SIZE, filename + 1);
    filename[0] = filename[1];
    filename[1] = filename[2];
    filename[2] = '/';
    freeEntry->file = FileAddRelative(FileGetName(cacheDir),
                                           FileGetNameLength(cacheDir),
                                           filename,
                                           FILENAME_DIGEST_SIZE / 5 * 8);
    if (!freeEntry->file)
    {
        return OUT_OF_MEMORY;
    }
    error = ByteVectorInit(&freeEntry->dependencies, 0);
    if (error)
    {
        return error;
    }
    memcpy(freeEntry->hash, hash, FILENAME_DIGEST_SIZE);
    freeEntry->uptodate = false;
    freeEntry->newEntry = true;
    *ref = refFromSize((size_t)(freeEntry - entries));
    return NO_ERROR;
}

ErrorCode CacheSetUptodate(cacheref ref)
{
    Entry *restrict entry = getEntry(ref);
    fileref file;
    ErrorCode error;
    const byte *restrict depend;
    const byte *restrict dependLimit;
    byte *restrict data;
    size_t size;
    size_t filenameLength;

    assert(!entry->uptodate);

    depend = ByteVectorGetPointer(&entry->dependencies, 0);
    dependLimit = depend + ByteVectorSize(&entry->dependencies);

    size = sizeof(size_t) + FILENAME_DIGEST_SIZE;
    while (depend < dependLimit)
    {
        file = *(fileref*)depend;
        /* TODO: Check if file has changed */
        size += FileGetNameLength(file) + sizeof(size_t) +
            FileGetStatusBlobSize();
        depend += sizeof(fileref) + FileGetStatusBlobSize();
    }

    data = (byte*)malloc(size);
    if (!data)
    {
        return OUT_OF_MEMORY;
    }
    *(size_t*)data = size;
    memcpy(data + sizeof(size_t), entry->hash, FILENAME_DIGEST_SIZE);
    size = sizeof(size_t) + FILENAME_DIGEST_SIZE;
    depend = ByteVectorGetPointer(&entry->dependencies, 0);
    while (depend < dependLimit)
    {
        file = *(fileref*)depend;
        depend += sizeof(fileref);
        filenameLength = FileGetNameLength(file);
        *(size_t*)&data[size] = filenameLength;
        size += sizeof(size_t);
        memcpy(data + size, FileGetName(file), filenameLength);
        size += filenameLength;
        memcpy(data + size, depend, FileGetStatusBlobSize());
        size += FileGetStatusBlobSize();
        depend += FileGetStatusBlobSize();
    }

    error = FileOpenAppend(cacheIndexTemp);
    if (error)
    {
        free(data);
        return error;
    }
    error = FileWrite(cacheIndexTemp, data, size);
    free(data);
    if (error)
    {
        return error;
    }
    entry->uptodate = true;
    return NO_ERROR;
}

ErrorCode CacheAddDependency(cacheref ref, fileref file)
{
    Entry *entry = getEntry(ref);
    size_t oldSize = ByteVectorSize(&entry->dependencies);
    const byte *blob;
    byte *p;
    ErrorCode error;

    assert(!CacheUptodate(ref));
    error = FileGetStatusBlob(file, &blob);
    if (error)
    {
        return error;
    }
    error = ByteVectorGrow(&entry->dependencies,
                           sizeof(fileref) + FileGetStatusBlobSize());
    if (error)
    {
        return error;
    }
    p = ByteVectorGetPointer(&entry->dependencies, oldSize);
    *(fileref*)p = file;
    memcpy(p + sizeof(fileref), blob, FileGetStatusBlobSize());
    return NO_ERROR;
}

boolean CacheUptodate(cacheref ref)
{
    return getEntry(ref)->uptodate;
}

boolean CacheIsNewEntry(cacheref ref)
{
    return getEntry(ref)->newEntry;
}

fileref CacheGetFile(cacheref ref)
{
    return getEntry(ref)->file;
}
