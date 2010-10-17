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
    fileref directory;
    boolean uptodate;
    boolean newEntry;
    bytevector dependencies;
} Entry;

typedef struct
{
    fileref file;
    size_t size;
    filetime_t mtime;
} Dependency;

static fileref cacheDir;
static Entry entries[16];


static Entry *getEntry(cacheref ref)
{
    assert(entries[sizeFromRef(ref)].directory);
    return &entries[sizeFromRef(ref)];
}

ErrorCode CacheInit(void)
{
    cacheDir = FileAdd(".don/cache", 10);
    return cacheDir ? NO_ERROR : OUT_OF_MEMORY;
}

void CacheDispose(void)
{
    Entry *entry;
    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (entry->directory)
        {
            ByteVectorDispose(&entry->dependencies);
        }
    }
}

ErrorCode CacheGet(const byte *hash, cacheref *ref)
{
    Entry *entry;
    Entry *freeEntry = null;
    char directoryName[FILENAME_DIGEST_SIZE / 5 * 8 + 1];
    ErrorCode error;

    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (!entry->directory)
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

    UtilBase32(hash, FILENAME_DIGEST_SIZE, directoryName + 1);
    directoryName[0] = directoryName[1];
    directoryName[1] = directoryName[2];
    directoryName[2] = '/';
    freeEntry->directory = FileAddRelative(FileGetName(cacheDir),
                                           FileGetNameLength(cacheDir),
                                           directoryName,
                                           FILENAME_DIGEST_SIZE / 5 * 8);
    if (!freeEntry->directory)
    {
        return OUT_OF_MEMORY;
    }
    error = ByteVectorInit(&freeEntry->dependencies, sizeof(Dependency));
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

ErrorCode CacheAddDependency(cacheref ref, fileref file)
{
    Entry *entry = getEntry(ref);
    Dependency *dependency;
    size_t oldSize = ByteVectorSize(&entry->dependencies);
    ErrorCode error;

    assert(!CacheUptodate(ref));
    error = ByteVectorGrow(&entry->dependencies, sizeof(Dependency));
    if (error)
    {
        return error;
    }
    dependency = (Dependency*)ByteVectorGetPointer(&entry->dependencies, oldSize);
    dependency->file = file;
    error = FileStat(file, &dependency->size, &dependency->mtime);
    if (error)
    {
        ByteVectorSetSize(&entry->dependencies, oldSize);
        return error;
    }
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

fileref CacheGetDirectory(cacheref ref)
{
    return getEntry(ref)->directory;
}
