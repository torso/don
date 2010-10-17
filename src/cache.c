#include "memory.h"
#include "common.h"
#include "cache.h"
#include "file.h"
#include "hash.h"
#include "util.h"

typedef struct
{
    byte hash[DIGEST_SIZE];
    fileref directory;
    boolean uptodate;
} Entry;

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
}

ErrorCode CacheGet(const byte *hash, cacheref *ref)
{
    Entry *entry;
    Entry *freeEntry = null;
    char directoryName[DIGEST_SIZE * 2];

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
            if (!memcmp(hash, entry->hash, DIGEST_SIZE))
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

    UtilBase32(hash, DIGEST_SIZE - (DIGEST_SIZE % 5), directoryName);
    freeEntry->directory = FileAddRelative(FileGetName(cacheDir),
                                           FileGetNameLength(cacheDir),
                                           directoryName, DIGEST_SIZE / 5 * 8);
    if (!freeEntry->directory)
    {
        return OUT_OF_MEMORY;
    }
    memcpy(freeEntry->hash, hash, DIGEST_SIZE);
    freeEntry->uptodate = false;
    *ref = refFromSize((size_t)(freeEntry - entries));
    return NO_ERROR;
}

boolean CacheUptodate(cacheref ref)
{
    return getEntry(ref)->uptodate;
}

fileref CacheGetDirectory(cacheref ref)
{
    return getEntry(ref)->directory;
}
