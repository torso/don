#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#ifdef VALGRIND
#include <valgrind/memcheck.h>
#endif
#include "memory.h"
#include "common.h"
#include "bytevector.h"
#include "cache.h"
#include "fail.h"
#include "file.h"
#include "hash.h"
#include "heap.h"
#include "log.h"
#include "util.h"

/*
  Must be evenly divisible by 5 for simple base32 encoding.
*/
#define CACHE_DIGEST_SIZE 40
#define CACHE_FILENAME_LENGTH (CACHE_DIGEST_SIZE / 5 * 8)

#define TAG 0x646f6e00

/*
  The layout of this struct results in simple verification of compatibility.
*/
typedef struct
{
    byte ignoredByte;
    size_t ignoredSize;
    uint sequenceNumber;
    uint tag;
} FileHeader;

typedef struct
{
    char *path;
    size_t pathLength;
    File file;
    FileHeader header;
    const byte *data;
    size_t size;
} IndexInfo;

typedef struct
{
    uint pathLength;
    FileStatus status;
} Dependency;

typedef struct
{
    size_t size;
    byte hash[CACHE_DIGEST_SIZE];
    uint dependencyCount;
    uint outLength;
    uint errLength;
    Dependency dependencies[1]; /* dependencyCount number of entries */
    /* paths for dependencies */
    /* out[outLength] */
    /* err[errLength] */
} Entry;

typedef struct
{
    byte hash[CACHE_DIGEST_SIZE];
    size_t entry;
} TableEntry;

static boolean initialised;
static const byte *oldEntries;
static size_t oldEntriesSize;
static bytevector newEntries;
static size_t entryCount;
static TableEntry table[0x10000];
static size_t tableMask = 0xffff;

static char *cacheDir;
static size_t cacheDirLength;
static IndexInfo infoRead;
static IndexInfo infoWrite;
static IndexInfo infoRewrite;


static size_t tableIndex(const byte *hash)
{
    return *(const size_t*)hash & tableMask;
}

static const Entry *getEntry(size_t entry)
{
    if (entry < oldEntriesSize)
    {
        return (const Entry*)(oldEntries + entry);
    }
    return (const Entry*)BVGetPointer(&newEntries, entry - oldEntriesSize);
}

static void deleteIndex(IndexInfo *info)
{
    FileClose(&info->file);
    FileDelete(info->path, info->pathLength);
    info->header.sequenceNumber = 0;
    info->data = null;
}

static void createIndex(IndexInfo *info, uint sequenceNumber)
{
    memset(&info->header, 0, sizeof(info->header));
    info->header.sequenceNumber = sequenceNumber;
    info->header.tag = TAG;
    assert(!FileIsOpen(&info->file));
    FileOpenAppend(&info->file, info->path, info->pathLength, true);
    FileWrite(&info->file, (const byte*)&info->header, sizeof(info->header));
}

static void writeIndex(IndexInfo *info, uint sequenceNumber,
                       const byte *entryData1, size_t entryData1Size,
                       const byte *entryData2, size_t entryData2Size)
{
    size_t i;
    createIndex(info, sequenceNumber);
    for (i = 0; i <= tableMask; i++)
    {
        if (table[i].entry)
        {
            size_t offset = table[i].entry - 1;
            const byte *e;
            assert(offset < entryData1Size + entryData2Size);
            if (offset < entryData1Size)
            {
                e = entryData1 + offset;
            }
            else
            {
                e = entryData2 + offset - entryData1Size;
            }
            FileWrite(&info->file, e, ((const Entry*)e)->size);
        }
    }
    FileClose(&info->file);
}

static void initIndex(uint slot, IndexInfo *info)
{
    char filename[6];
    memset(info, 0, sizeof(*info));
    memcpy(filename, "index", 5);
    filename[5] = (char)('0' + slot);
    info->path = FileCreatePath(cacheDir, cacheDirLength, filename,
                                sizeof(filename), null, 0, &info->pathLength);
}

static boolean openIndex(IndexInfo *info)
{
    FileHeader *header;
    if (!FileTryOpen(&info->file, info->path, info->pathLength))
    {
        return false;
    }
    FileMMap(&info->file, &info->data, &info->size);
    header = (FileHeader*)info->data;
    if (info->size <= sizeof(FileHeader) || header->tag != TAG ||
        !header->sequenceNumber)
    {
        deleteIndex(info);
        return false;
    }
    info->header = *header;
    info->data += sizeof(FileHeader);
    info->size -= sizeof(FileHeader);
    return true;
}

static void buildTable(const byte *data, size_t size, size_t offset)
{
    size_t i;
    size_t j;
    for (i = 0; i < size;)
    {
        const Entry *e = (const Entry*)(data + i);
        for (j = tableIndex(e->hash);; j = (j + 1) & tableMask)
        {
            if (!table[j].entry)
            {
                assert(entryCount < tableMask); /* TODO: Grow table */
                entryCount++;
                break;
            }
            if (!memcmp(table[j].hash, e->hash, CACHE_DIGEST_SIZE))
            {
                break;
            }
        }
        memcpy(table[j].hash, e->hash, CACHE_DIGEST_SIZE);
        table[j].entry = offset + i + 1;
        i += e->size;
    }
}

static void loadIndex(IndexInfo *info)
{
    assert(info->data);
    assert(!entryCount);
    oldEntries = info->data;
    oldEntriesSize = info->size;
    buildTable(oldEntries, oldEntriesSize, 0);
}

static void rebuildIndex(IndexInfo *src1, IndexInfo *src2, IndexInfo *dst)
{
    if (src1->header.sequenceNumber > src2->header.sequenceNumber)
    {
        IndexInfo *tmp = src1;
        src1 = src2;
        src2 = tmp;
    }

    buildTable(src1->data, src1->size, 0);
    buildTable(src2->data, src2->size, src1->size);
    writeIndex(dst, src2->header.sequenceNumber + 1,
               src1->data, src1->size, src2->data, src2->size);
    if (!openIndex(dst))
    {
        Fail("Error reopening rebuilt cache index.\n");
    }
    deleteIndex(src1);
    deleteIndex(src2);
    oldEntries = dst->data;
    oldEntriesSize = dst->size;
    infoRead = *dst;
    infoWrite = *src1;
    infoRewrite = *src2;
    createIndex(&infoWrite, dst->header.sequenceNumber + 1);
}

void CacheInit(char *cacheDirectory, size_t cacheDirectoryLength)
{
    IndexInfo info1;
    IndexInfo info2;
    IndexInfo info3;

    BVInit(&newEntries, 1024);

    cacheDir = cacheDirectory;
    cacheDirLength = cacheDirectoryLength;
    FilePinDirectory(cacheDirectory, cacheDirectoryLength);

    initIndex(1, &info1);
    initIndex(2, &info2);
    initIndex(3, &info3);
    openIndex(&info1);
    openIndex(&info2);
    openIndex(&info3);
    if (info1.header.sequenceNumber && info2.header.sequenceNumber &&
        info3.header.sequenceNumber)
    {
        /* An index rebuild was terminated prematurely. Delete the newest file
           and do a new attempt to rebuild it. */
        if (info1.header.sequenceNumber > info2.header.sequenceNumber &&
            info1.header.sequenceNumber > info3.header.sequenceNumber)
        {
            deleteIndex(&info1);
        }
        if (info2.header.sequenceNumber > info1.header.sequenceNumber &&
            info2.header.sequenceNumber > info3.header.sequenceNumber)
        {
            deleteIndex(&info2);
        }
        if (info3.header.sequenceNumber > info1.header.sequenceNumber &&
            info3.header.sequenceNumber > info2.header.sequenceNumber)
        {
            deleteIndex(&info3);
        }
    }
    /* If there are two index files, rebuild them into a single one and delete
       the existing ones. */
    if (info1.header.sequenceNumber && info2.header.sequenceNumber)
    {
        rebuildIndex(&info1, &info2, &info3);
    }
    else if (info1.header.sequenceNumber && info3.header.sequenceNumber)
    {
        rebuildIndex(&info1, &info3, &info2);
    }
    else if (info2.header.sequenceNumber && info3.header.sequenceNumber)
    {
        rebuildIndex(&info2, &info3, &info1);
    }
    else if (info1.header.sequenceNumber)
    {
        loadIndex(&info1);
        createIndex(&info2, info1.header.sequenceNumber + 1);
        infoRead = info1;
        infoWrite = info2;
        infoRewrite = info3;
    }
    else if (info2.header.sequenceNumber)
    {
        loadIndex(&info2);
        createIndex(&info1, info2.header.sequenceNumber + 1);
        infoRead = info2;
        infoWrite = info1;
        infoRewrite = info3;
    }
    else if (info3.header.sequenceNumber)
    {
        loadIndex(&info3);
        createIndex(&info1, info3.header.sequenceNumber + 1);
        infoRead = info3;
        infoWrite = info1;
        infoRewrite = info2;
    }
    else
    {
        free(info3.path);
        createIndex(&info1, 1);
        infoWrite = info1;
        infoRewrite = info2;
    }

    initialised = true;
}

void CacheDispose(void)
{
    if (!initialised)
    {
        return;
    }

    assert(FileIsOpen(&infoWrite.file));
    FileClose(&infoWrite.file);

    writeIndex(&infoRewrite, infoWrite.header.sequenceNumber + 1,
               oldEntries, oldEntriesSize,
               BVGetPointer(&newEntries, 0), BVSize(&newEntries));

    if (infoRead.header.sequenceNumber)
    {
        FileClose(&infoRead.file);
        FileDelete(infoRead.path, infoRead.pathLength);
        free(infoRead.path);
    }
    FileDelete(infoWrite.path, infoWrite.pathLength);
    free(infoWrite.path);
    free(infoRewrite.path);

    BVDispose(&newEntries);
    FileUnpinDirectory(cacheDir, cacheDirLength);
    free(cacheDir);
}

void CacheGet(const byte *hash, boolean echoCachedOutput,
              boolean *uptodate, char **path, size_t *pathLength)
{
    const char *p;
    char filename[CACHE_FILENAME_LENGTH + 1];
    const Entry *entry;
    size_t i;

    UtilBase32(hash, CACHE_DIGEST_SIZE, filename + 1);
    filename[0] = filename[1];
    filename[1] = filename[2];
    filename[2] = '/';
    *path = FileCreatePath(cacheDir, cacheDirLength,
                           filename, CACHE_FILENAME_LENGTH + 1,
                           null, 0,
                           pathLength);

    for (i = tableIndex(hash);; i = (i + 1) & tableMask)
    {
        if (!table[i].entry)
        {
            FileMkdir(*path, *pathLength);
            *uptodate = false;
            return;
        }
        if (!memcmp(table[i].hash, hash, CACHE_DIGEST_SIZE))
        {
            entry = getEntry(table[i].entry - 1);
            break;
        }
    }

    p = (const char*)entry + offsetof(Entry, dependencies) +
        entry->dependencyCount * sizeof(*entry->dependencies);
    for (i = 0; i < entry->dependencyCount; i++)
    {
        uint length = entry->dependencies[i].pathLength;
        if (FileHasChanged(p, length, &entry->dependencies[i].status))
        {
            /* TODO: Mark entry as outdated, in case this process is killed. */
            *uptodate = false;
            return;
        }
        p += length;
    }

    *uptodate = true;
    if (echoCachedOutput)
    {
        if (entry->outLength)
        {
            LogPrintAutoNewline(p, entry->outLength);
            p += entry->outLength;
        }
        if (entry->errLength)
        {
            LogPrintErrAutoNewline(p, entry->errLength);
        }
    }
}

static void appendString(vref value)
{
    uint length = (uint)HeapStringLength(value);
    BVAddUint(&newEntries, length);
    if (length)
    {
        BVReserveAppendSize(&newEntries, length);
        HeapWriteString(value, (char*)BVGetAppendPointer(&newEntries));
        BVGrow(&newEntries, length);
    }
}

void CacheSetUptodate(const char *path, size_t pathLength, vref dependencies,
                      vref out, vref err)
{
    Entry *entry;
    uint dependencyCount = (uint)HeapCollectionSize(dependencies);
    size_t entryStart;
    size_t i;
    byte hash[CACHE_FILENAME_LENGTH];

    assert(path);
    assert(pathLength > CACHE_FILENAME_LENGTH);
    hash[0] = (byte)path[pathLength - CACHE_FILENAME_LENGTH - 1];
    hash[1] = (byte)path[pathLength - CACHE_FILENAME_LENGTH];
    assert(path[pathLength - CACHE_FILENAME_LENGTH + 1] == '/');
    memcpy(hash + 2, path + pathLength - CACHE_FILENAME_LENGTH + 2,
           CACHE_FILENAME_LENGTH - 2);
    UtilDecodeBase32((const char*)hash, CACHE_FILENAME_LENGTH, hash);

    assert(entryCount < tableMask); /* TODO: Grow table */
    for (i = tableIndex(hash);; i = (i + 1) & tableMask)
    {
        if (!table[i].entry)
        {
            entryCount++;
            memcpy(table[i].hash, hash, CACHE_DIGEST_SIZE);
            break;
        }
        if (!memcmp(table[i].hash, hash, CACHE_DIGEST_SIZE))
        {
            break;
        }
    }
    table[i].entry = oldEntriesSize + BVSize(&newEntries) + 1;

    entryStart = BVSize(&newEntries);
    BVGrow(&newEntries, offsetof(Entry, dependencies) +
           dependencyCount * sizeof(entry->dependencies));
    entry = (Entry*)BVGetPointer(&newEntries, entryStart);
    memcpy(entry->hash, hash, CACHE_DIGEST_SIZE);
    entry->dependencyCount = dependencyCount;
    entry->outLength = (uint)HeapStringLength(out);
    entry->errLength = (uint)HeapStringLength(err);
    for (i = 0; i < dependencyCount; i++)
    {
        vref value;
        uint length;
        size_t pathStart;
        if (!HeapCollectionGet(dependencies, HeapBoxSize(i), &value))
        {
            assert(false); /* TODO: Error handling. */
        }
        assert(HeapIsFile(value));
        length = (uint)HeapStringLength(value);

        pathStart = BVSize(&newEntries);
        BVReserveAppendSize(&newEntries, length);
        HeapWriteString(value, (char*)BVGetAppendPointer(&newEntries));
        BVGrow(&newEntries, length);

        entry = (Entry*)BVGetPointer(&newEntries, entryStart);
        entry->dependencies[i].pathLength = length;
        memcpy(&entry->dependencies[i].status,
               FileGetStatus((const char*)BVGetPointer(&newEntries, pathStart),
                             length),
               sizeof(FileStatus));
    }
    appendString(out);
    appendString(err);
    /* TODO: Add padding for alignment */
    entry = (Entry*)BVGetPointer(&newEntries, entryStart);
    entry->size = BVSize(&newEntries) - entryStart;
#ifdef VALGRIND
    /* Ignore undefined padding */
    VALGRIND_MAKE_MEM_DEFINED(entry, offsetof(Entry, dependencies));
#endif
    FileWrite(&infoWrite.file, (const byte*)entry, entry->size);
}
