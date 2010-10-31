#include <stdarg.h>
#include "memory.h"
#include "common.h"
#include "bytevector.h"
#include "cache.h"
#include "file.h"
#include "hash.h"
#include "log.h"
#include "util.h"

#define FILENAME_DIGEST_SIZE (DIGEST_SIZE - (DIGEST_SIZE % 5))

typedef struct
{
    byte hash[FILENAME_DIGEST_SIZE];
    fileref file;
    boolean newEntry;
    boolean written;
    bytevector dependencies;
    size_t outLength;
    size_t errLength;
    char *output;
} Entry;

static fileref cacheDir;
static fileref cacheIndex;
static fileref cacheIndexOut;
static Entry entries[256];


static Entry *getEntry(cacheref ref)
{
    assert(entries[sizeFromRef(ref)].file);
    return &entries[sizeFromRef(ref)];
}

static void clearEntry(Entry *entry)
{
    assert(entry->file);
    ByteVectorDispose(&entry->dependencies);
    free(entry->output);
    entry->file = 0;
}

static void addDependency(Entry *entry, fileref file, const byte *blob)
{
    size_t oldSize = ByteVectorSize(&entry->dependencies);
    byte *p;

    assert(entry->newEntry);
    ByteVectorGrow(&entry->dependencies,
                   sizeof(fileref) + FileGetStatusBlobSize());
    p = ByteVectorGetPointer(&entry->dependencies, oldSize);
    *(fileref*)p = file;
    memcpy(p + sizeof(fileref), blob, FileGetStatusBlobSize());
}

static void writeEntry(Entry *restrict entry, fileref indexFile)
{
    fileref file;
    const byte *restrict depend;
    const byte *restrict dependLimit;
    byte *restrict data;
    size_t size;
    size_t filenameLength;

    depend = ByteVectorGetPointer(&entry->dependencies, 0);
    dependLimit = depend + ByteVectorSize(&entry->dependencies);

    size = 3 * sizeof(size_t) + FILENAME_DIGEST_SIZE +
        entry->outLength + entry->errLength;
    while (depend < dependLimit)
    {
        file = *(fileref*)depend;
        /* TODO: Check if file has changed */
        size += FileGetNameLength(file) + sizeof(size_t) +
            FileGetStatusBlobSize();
        depend += sizeof(fileref) + FileGetStatusBlobSize();
    }

    data = (byte*)malloc(size);
    *(size_t*)data = size;
    memcpy(data + sizeof(size_t), entry->hash, FILENAME_DIGEST_SIZE);
    size = sizeof(size_t) + FILENAME_DIGEST_SIZE;
    ((size_t*)(data + size))[0] = entry->outLength;
    ((size_t*)(data + size))[1] = entry->errLength;
    size += 2 * sizeof(size_t);
    if (entry->outLength || entry->errLength)
    {
        assert(entry->output);
        memcpy(data + size, entry->output, entry->outLength + entry->errLength);
        size += entry->outLength + entry->errLength;
    }
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

    FileOpenAppend(indexFile);
    FileWrite(indexFile, data, size);
    free(data);
    entry->written = true;
}

static boolean readIndex(fileref file)
{
    cacheref ref;
    Entry *entry;
    const byte *data;
    const byte *limit;
    size_t size;
    size_t entrySize;
    size_t filenameLength;
    fileref dependFile;

    FileMMap(file, &data, &size, false);
    if (!data)
    {
        return false;
    }
    while (size)
    {
        if (size < sizeof(size_t))
        {
            break;
        }
        entrySize = *(size_t*)data;
        if (entrySize < 3 * sizeof(size_t) + FILENAME_DIGEST_SIZE ||
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
        data += FILENAME_DIGEST_SIZE;
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
            if ((size_t)(limit - data) < filenameLength + FileGetStatusBlobSize())
            {
                clearEntry(entry);
                break;
            }
            dependFile = FileAdd((const char*)data, filenameLength);
            data += filenameLength;
            addDependency(entry, dependFile, data);
            data += FileGetStatusBlobSize();
        }
        entry->newEntry = false;
    }
    FileMUnmap(file);
    return true;
}


void CacheInit(fileref cacheDirectory)
{
    Entry *entry;
    fileref tempfile;
    const char *base = FileGetName(cacheDirectory);
    size_t baseLength = FileGetNameLength(cacheDirectory);

    cacheDir = FileAddRelative(base, baseLength, "cache/", 6);
    cacheIndex = FileAddRelative(base, baseLength, "cache/index", 11);
    cacheIndexOut = FileAddRelative(base, baseLength, "cache/index.1", 13);
    tempfile = FileAddRelative(base, baseLength, "cache/index.2", 13);
    FileMkdir(cacheDir);
    FileDelete(tempfile);
    readIndex(cacheIndex);
    if (readIndex(cacheIndexOut))
    {
        FileOpenAppend(tempfile);
        for (entry = entries + 1;
             entry < entries + sizeof(entries) / sizeof(Entry);
             entry++)
        {
            if (entry->file)
            {
                writeEntry(entry, tempfile);
                entry->written = false;
            }
        }
        FileCloseSync(cacheIndexOut);
        FileRename(tempfile, cacheIndexOut, true);
        FileRename(cacheIndexOut, cacheIndex, true);
    }
}

void CacheDispose(void)
{
    Entry *entry;

    if (!cacheIndexOut)
    {
        return;
    }

    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (entry->file)
        {
            if (!entry->written && !entry->newEntry)
            {
                writeEntry(entry, cacheIndexOut);
            }
            clearEntry(entry);
        }
    }
    FileCloseSync(cacheIndexOut);
    FileDelete(cacheIndex);
    FileRename(cacheIndexOut, cacheIndex, false);
}

cacheref CacheGet(const byte *hash)
{
    Entry *entry;
    Entry *freeEntry = null;
    char filename[FILENAME_DIGEST_SIZE / 5 * 8 + 1];

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
                return refFromSize((size_t)(entry - entries));
            }
        }
    }

    assert(freeEntry); /* TODO: Grow index. */

    UtilBase32(hash, FILENAME_DIGEST_SIZE, filename + 1);
    filename[0] = filename[1];
    filename[1] = filename[2];
    filename[2] = '/';
    freeEntry->file = FileAddRelative(FileGetName(cacheDir),
                                      FileGetNameLength(cacheDir),
                                      filename,
                                      FILENAME_DIGEST_SIZE / 5 * 8);
    ByteVectorInit(&freeEntry->dependencies, 0);
    memcpy(freeEntry->hash, hash, FILENAME_DIGEST_SIZE);
    freeEntry->newEntry = true;
    freeEntry->written = false;
    return refFromSize((size_t)(freeEntry - entries));
}

cacheref CacheGetFromFile(fileref file)
{
    Entry *entry;

    assert(file);
    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (entry->file == file)
        {
            return refFromSize((size_t)(entry - entries));
        }
    }
    assert(false); /* TODO: Error handling. */
    return 0;
}

void CacheAddDependency(cacheref ref, fileref file)
{
    addDependency(getEntry(ref), file, FileGetStatusBlob(file));
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
    writeEntry(entry, cacheIndexOut);
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
    fileref file;
    const byte *restrict depend;
    const byte *restrict dependLimit;

    if (entry->newEntry)
    {
        return false;
    }

    depend = ByteVectorGetPointer(&entry->dependencies, 0);
    dependLimit = depend + ByteVectorSize(&entry->dependencies);
    while (depend < dependLimit)
    {
        file = *(fileref*)depend;
        depend += sizeof(fileref);
        if (FileHasChanged(file, depend))
        {
            entry->newEntry = true;
            entry->written = false;
            ByteVectorSetSize(&entry->dependencies, 0);
            entry->outLength = 0;
            entry->errLength = 0;
            free(entry->output);
            return false;
        }
        depend += FileGetStatusBlobSize();
    }
    return true;
}

boolean CacheIsNewEntry(cacheref ref)
{
    return getEntry(ref)->newEntry;
}

fileref CacheGetFile(cacheref ref)
{
    return getEntry(ref)->file;
}
