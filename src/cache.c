#include <stdarg.h>
#include "memory.h"
#include "common.h"
#include "bytevector.h"
#include "cache.h"
#include "file.h"
#include "hash.h"
#include "log.h"
#include "util.h"

/*
  Waste a few bits of the hash to get a length evenly divisible by 5 for simple
  base32 encoding.
*/
#define CACHE_DIGEST_SIZE (DIGEST_SIZE - (DIGEST_SIZE % 5))
#define CACHE_FILENAME_LENGTH (CACHE_DIGEST_SIZE / 5 * 8)

typedef struct
{
    byte hash[CACHE_DIGEST_SIZE];
    boolean valid;
    boolean newEntry;
    boolean written;
    bytevector dependencies;
    size_t outLength;
    size_t errLength;
    char *output;
} Entry;

static boolean initialised;
static Entry entries[32768];

static char *cacheDir;
static size_t cacheDirLength;
static char *cacheIndexPath;
static size_t cacheIndexLength;
static char *cacheIndexOutPath;
static size_t cacheIndexOutLength;

static File cacheIndexOut;
static bytevector outBuffer;


static Entry *getEntry(cacheref ref)
{
    assert(entries[sizeFromRef(ref)].valid);
    return &entries[sizeFromRef(ref)];
}

static void clearEntry(Entry *entry)
{
    assert(entry->valid);
    BVDispose(&entry->dependencies);
    free(entry->output);
    entry->valid = false;
}

static void addDependency(Entry *entry, const char *path, size_t length,
                          const byte *blob)
{
    assert(entry->newEntry);
    BVReserveAppendSize(&entry->dependencies,
                        sizeof(size_t) + length + FileStatusBlobSize());
    BVAddSize(&entry->dependencies, length);
    BVAddData(&entry->dependencies, (const byte*)path, length);
    BVAddData(&entry->dependencies, blob, FileStatusBlobSize());
}

static void writeEntry(Entry *restrict entry)
{
    size_t size;

    assert(!BVSize(&outBuffer));

    BVAddSize(&outBuffer, 0);
    BVAddData(&outBuffer, entry->hash, sizeof(entry->hash));
    BVAddSize(&outBuffer, entry->outLength);
    BVAddSize(&outBuffer, entry->errLength);
    BVAddData(&outBuffer, (const byte*)entry->output, entry->outLength + entry->errLength);
    BVAddAll(&outBuffer, &entry->dependencies);

    size = BVSize(&outBuffer);
    BVSetSizeAt(&outBuffer, 0, size);

    if (!FileIsOpen(&cacheIndexOut))
    {
        FileOpenAppend(&cacheIndexOut, cacheIndexOutPath, cacheIndexOutLength, true);
    }
    FileWrite(&cacheIndexOut, BVGetPointer(&outBuffer, 0), size);
    BVSetSize(&outBuffer, 0);
    entry->written = true;
}

static boolean readIndex(const char *path, size_t pathLength)
{
    File file;
    cacheref ref;
    Entry *entry;
    const byte *data;
    const byte *limit;
    size_t size;
    size_t entrySize;
    size_t filenameLength;

    if (!FileTryOpen(&file, path, pathLength))
    {
        return false;
    }
    FileMMap(&file, &data, &size);
    assert(data);
    while (size)
    {
        if (size < sizeof(size_t))
        {
            break;
        }
        entrySize = *(size_t*)data;
        if (entrySize < 3 * sizeof(size_t) + CACHE_DIGEST_SIZE ||
            entrySize > size)
        {
            break;
        }
        limit = data + entrySize;
        size -= entrySize;
        data += sizeof(size_t);
        ref = CacheGet(data);
        entry = getEntry(ref);
        if (!entry->newEntry)
        {
            clearEntry(entry);
            ref = CacheGet(data);
            entry = getEntry(ref);
        }
        data += CACHE_DIGEST_SIZE;
        entry->outLength = ((size_t*)data)[0];
        entry->errLength = ((size_t*)data)[1];
        data += 2 * sizeof(size_t);
        entry->output = null;
        if (entry->outLength || entry->errLength)
        {
            if (data + entry->outLength + entry->errLength > limit)
            {
                clearEntry(entry);
                break;
            }
            entry->output = (char*)malloc(entry->outLength + entry->errLength);
            memcpy(entry->output, data, entry->outLength + entry->errLength);
            data += entry->outLength + entry->errLength;
        }
        while (data < limit)
        {
            if ((size_t)(limit - data) < sizeof(size_t))
            {
                clearEntry(entry);
                break;
            }
            filenameLength = *(size_t*)data;
            data += sizeof(size_t);
            if ((size_t)(limit - data) < filenameLength + FileStatusBlobSize())
            {
                clearEntry(entry);
                break;
            }
            path = (const char*)data;
            data += filenameLength;
            addDependency(entry, path, filenameLength, data);
            data += FileStatusBlobSize();
        }
        entry->newEntry = false;
    }
    FileClose(&file);
    return true;
}

void CacheInit(char *cacheDirectory, size_t cacheDirectoryLength)
{
    char *tempIndex;
    size_t tempIndexLength;
    size_t i;

    cacheDir = cacheDirectory;
    cacheDirLength = cacheDirectoryLength;
    FilePinDirectory(cacheDirectory, cacheDirectoryLength);
    cacheIndexPath = FileCreatePath(cacheDirectory, cacheDirectoryLength,
                                    "index", 5, null, 0, &cacheIndexLength);
    cacheIndexOutPath = FileCreatePath(cacheDirectory, cacheDirectoryLength,
                                       "index.1", 7, null, 0, &cacheIndexOutLength);
    BVInit(&outBuffer, 1024);
    tempIndex = FileCreatePath(cacheDirectory, cacheDirectoryLength,
                               "index.2", 7, null, 0, &tempIndexLength);
    FileDelete(tempIndex, tempIndexLength);
    readIndex(cacheIndexPath, cacheIndexLength);
    if (readIndex(cacheIndexOutPath, cacheIndexOutLength))
    {
        FileOpenAppend(&cacheIndexOut, tempIndex, tempIndexLength, true);
        for (i = 1; i < sizeof(entries) / sizeof(Entry); i++)
        {
            Entry *entry = entries + i;
            if (entry->valid)
            {
                writeEntry(entry);
                entry->written = false;
            }
        }
        FileClose(&cacheIndexOut);
        FileRename(tempIndex, tempIndexLength,
                   cacheIndexOutPath, cacheIndexOutLength);
        FileRename(cacheIndexOutPath, cacheIndexOutLength,
                   cacheIndexPath, cacheIndexLength);
    }
    free(tempIndex);
    initialised = true;
}

void CacheDispose(void)
{
    size_t i;

    if (!initialised)
    {
        return;
    }

    for (i = 1; i < sizeof(entries) / sizeof(Entry); i++)
    {
        Entry *entry = entries + i;
        if (entry->valid)
        {
            if (!entry->written && !entry->newEntry)
            {
                writeEntry(entry);
            }
            clearEntry(entry);
        }
    }
    if (FileIsOpen(&cacheIndexOut))
    {
        FileClose(&cacheIndexOut);
        FileRename(cacheIndexOutPath, cacheIndexOutLength,
                   cacheIndexPath, cacheIndexLength);
    }
    BVDispose(&outBuffer);
    FileUnpinDirectory(cacheDir, cacheDirLength);
    free(cacheDir);
    free(cacheIndexPath);
    free(cacheIndexOutPath);
}

cacheref CacheGet(const byte *hash)
{
    Entry *freeEntry = null;
    size_t i;

    for (i = 1; i < sizeof(entries) / sizeof(Entry); i++)
    {
        Entry *entry = entries + i;
        if (!entry->valid)
        {
            freeEntry = entry;
        }
        else
        {
            if (!memcmp(hash, entry->hash, CACHE_DIGEST_SIZE))
            {
                return refFromSize((size_t)(entry - entries));
            }
        }
    }

    assert(freeEntry); /* TODO: Grow index. */

    BVInit(&freeEntry->dependencies, 0);
    memcpy(freeEntry->hash, hash, CACHE_DIGEST_SIZE);
    freeEntry->valid = true;
    freeEntry->newEntry = true;
    freeEntry->written = false;
    return refFromSize((size_t)(freeEntry - entries));
}

cacheref CacheGetFromFile(const char *path, size_t pathLength)
{
    size_t i;
    char filename[CACHE_FILENAME_LENGTH];

    assert(path);
    assert(pathLength > CACHE_FILENAME_LENGTH);
    filename[0] = path[pathLength - CACHE_FILENAME_LENGTH - 1];
    filename[1] = path[pathLength - CACHE_FILENAME_LENGTH];
    assert(path[pathLength - CACHE_FILENAME_LENGTH + 1] == '/');
    memcpy(filename + 2, path + pathLength - CACHE_FILENAME_LENGTH + 2,
           CACHE_FILENAME_LENGTH - 2);
    UtilDecodeBase32(filename, CACHE_FILENAME_LENGTH, (byte*)filename);
    for (i = 1; i < sizeof(entries) / sizeof(Entry); i++)
    {
        Entry *entry = entries + i;
        if (!memcmp(entry->hash, filename, CACHE_DIGEST_SIZE))
        {
            return refFromSize((size_t)(entry - entries));
        }
    }
    return 0;
}

void CacheAddDependency(cacheref ref, const char *path, size_t length)
{
    addDependency(getEntry(ref), path, length, FileStatusBlob(path, length));
}

void CacheSetUptodate(cacheref ref, size_t outLength, size_t errLength,
                      char *output)
{
    Entry *entry = getEntry(ref);

    assert(entry->newEntry);
    assert(!entry->written);

    entry->outLength = outLength;
    entry->errLength = errLength;
    entry->output = output;
    entry->newEntry = false;
    writeEntry(entry);
}

void CacheEchoCachedOutput(cacheref ref)
{
    Entry *entry = getEntry(ref);

    assert(!entry->newEntry);

    if (entry->outLength)
    {
        LogPrintAutoNewline(entry->output, entry->outLength);
    }
    if (entry->errLength)
    {
        LogPrintErrAutoNewline(entry->output + entry->outLength,
                               entry->errLength);
    }
}

boolean CacheCheckUptodate(cacheref ref)
{
    Entry *entry = getEntry(ref);
    const char *path;
    size_t length;
    const byte *restrict depend;
    const byte *restrict dependLimit;

    if (entry->newEntry)
    {
        return false;
    }

    depend = BVGetPointer(&entry->dependencies, 0);
    dependLimit = depend + BVSize(&entry->dependencies);
    while (depend < dependLimit)
    {
        length = *(size_t*)depend;
        depend += sizeof(size_t);
        path = (const char*)depend;
        depend += length;
        if (FileHasChanged(path, length, depend))
        {
            entry->newEntry = true;
            entry->written = false;
            BVSetSize(&entry->dependencies, 0);
            entry->outLength = 0;
            entry->errLength = 0;
            free(entry->output);
            entry->output = null;
            return false;
        }
        depend += FileStatusBlobSize();
    }
    return true;
}

boolean CacheIsNewEntry(cacheref ref)
{
    return getEntry(ref)->newEntry;
}

char *CacheGetFile(cacheref ref, size_t *length)
{
    Entry *entry = getEntry(ref);
    char filename[CACHE_FILENAME_LENGTH + 1];
    char filename2[CACHE_FILENAME_LENGTH];
    UtilBase32(entry->hash, CACHE_DIGEST_SIZE, filename + 1);
    memcpy(filename2, filename + 1, CACHE_FILENAME_LENGTH);
    UtilDecodeBase32(filename2, CACHE_FILENAME_LENGTH, (byte*)filename2);
    assert(!memcmp(filename2, entry->hash, CACHE_DIGEST_SIZE));
    filename[0] = filename[1];
    filename[1] = filename[2];
    filename[2] = '/';
    return FileCreatePath(cacheDir, cacheDirLength,
                          filename, CACHE_FILENAME_LENGTH + 1,
                          null, 0,
                          length);
}
