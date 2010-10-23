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
    boolean written;
    bytevector dependencies;
} Entry;

static fileref cacheDir;
static fileref cacheIndex;
static fileref cacheIndexOut;
static Entry entries[16];


static Entry *getEntry(cacheref ref)
{
    assert(entries[sizeFromRef(ref)].file);
    return &entries[sizeFromRef(ref)];
}

static void clearEntry(Entry *entry)
{
    assert(entry->file);
    ByteVectorDispose(&entry->dependencies);
    entry->file = 0;
}

static void addDependency(Entry *entry, fileref file, const byte *blob)
{
    size_t oldSize = ByteVectorSize(&entry->dependencies);
    byte *p;

    assert(!entry->uptodate);
    ByteVectorGrow(&entry->dependencies,
                   sizeof(fileref) + FileGetStatusBlobSize());
    p = ByteVectorGetPointer(&entry->dependencies, oldSize);
    *(fileref*)p = file;
    memcpy(p + sizeof(fileref), blob, FileGetStatusBlobSize());
}

static ErrorCode writeEntry(Entry *restrict entry, fileref indexFile)
{
    fileref file;
    ErrorCode error;
    const byte *restrict depend;
    const byte *restrict dependLimit;
    byte *restrict data;
    size_t size;
    size_t filenameLength;

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

    error = FileOpenAppend(indexFile);
    if (error)
    {
        free(data);
        return error;
    }
    error = FileWrite(indexFile, data, size);
    free(data);
    if (!error)
    {
        entry->written = true;
    }
    return error;
}

static ErrorCode readIndex(fileref file)
{
    cacheref ref;
    Entry *entry;
    const byte *data;
    const byte *limit;
    size_t size;
    size_t entrySize;
    size_t filenameLength;
    fileref dependFile;
    ErrorCode error;

    error = FileMMap(file, &data, &size);
    if (error)
    {
        return error;
    }
    while (size)
    {
        if (size < sizeof(size_t))
        {
            break;
        }
        entrySize = *(size_t*)data;
        if (entrySize < sizeof(size_t) + FILENAME_DIGEST_SIZE)
        {
            break;
        }
        limit = data + entrySize;
        size -= entrySize;
        data += sizeof(size_t);
        CacheGet(data, &ref);
        entry = getEntry(ref);
        if (!entry->newEntry)
        {
            clearEntry(entry);
            CacheGet(data, &ref);
            entry = getEntry(ref);
        }
        data += FILENAME_DIGEST_SIZE;
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
        entry->uptodate = true;
        entry->newEntry = false;
    }
    return FileMUnmap(file);
}


ErrorCode CacheInit(void)
{
    Entry *entry;
    fileref tempfile;
    ErrorCode error;

    cacheDir = FileAdd(".don/cache", 10);
    cacheIndex = FileAdd(".don/cache/index", 16);
    cacheIndexOut = FileAdd(".don/cache/index.1", 18);
    tempfile = FileAdd(".don/cache/index.2", 18);
    error = FileMkdir(cacheDir);
    if (error)
    {
        return error;
    }
    error = FileDelete(tempfile);
    if (error)
    {
        return error;
    }
    error = readIndex(cacheIndex);
    if (error && error != FILE_NOT_FOUND)
    {
        return error;
    }
    error = readIndex(cacheIndexOut);
    if (!error)
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
        error = FileRename(tempfile, cacheIndexOut);
        if (error)
        {
            return error;
        }
        error = FileRename(cacheIndexOut, cacheIndex);
    }
    else if (error == FILE_NOT_FOUND)
    {
        error = NO_ERROR;
    }
    return error;
}

ErrorCode CacheDispose(void)
{
    Entry *entry;
    ErrorCode error;

    for (entry = entries + 1;
         entry < entries + sizeof(entries) / sizeof(Entry);
         entry++)
    {
        if (entry->file)
        {
            if (!entry->written)
            {
                writeEntry(entry, cacheIndexOut);
            }
            clearEntry(entry);
        }
    }
    FileCloseSync(cacheIndexOut);
    error = FileDelete(cacheIndex);
    if (error)
    {
        return error;
    }
    error = FileRename(cacheIndexOut, cacheIndex);
    return error == FILE_NOT_FOUND ? NO_ERROR : error;
}

void CacheGet(const byte *hash, cacheref *ref)
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
                *ref = refFromSize((size_t)(entry - entries));
                return;
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
    freeEntry->uptodate = false;
    freeEntry->newEntry = true;
    freeEntry->written = false;
    *ref = refFromSize((size_t)(freeEntry - entries));
}

ErrorCode CacheSetUptodate(cacheref ref)
{
    Entry *entry = getEntry(ref);
    assert(!entry->uptodate);
    assert(!entry->written);
    entry->uptodate = true;
    return writeEntry(entry, cacheIndexOut);
}

ErrorCode CacheAddDependency(cacheref ref, fileref file)
{
    const byte *blob;
    ErrorCode error = FileGetStatusBlob(file, &blob);
    if (error)
    {
        return error;
    }
    addDependency(getEntry(ref), file, blob);
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
