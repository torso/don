#include <string.h>
#include "common.h"
#include "stringpool.h"
#include "util.h"

#define TABLE_SIZE 0
#define TABLE_DATA_BEGIN 2

#define TABLE_ENTRY_HASH 0
#define TABLE_ENTRY_VALUE 1
#define TABLE_ENTRY_SIZE 2

static char *stringData = null;
static size_t dataSize = 0;
static uint *table = null;


static void checkTable(const uint *t)
{
    assert(t);
    assert(t[TABLE_SIZE] > TABLE_DATA_BEGIN);
    assert((t[TABLE_SIZE] - TABLE_DATA_BEGIN) % TABLE_ENTRY_SIZE == 0);
}

static uint getTableSize(const uint *t)
{
    checkTable(t);
    return (t[TABLE_SIZE] - TABLE_DATA_BEGIN) / TABLE_ENTRY_SIZE;
}

static void checkSlot(const uint *t, uint slot)
{
    assert(getTableSize(t) > slot);
}

static uint getSlotForHash(const uint *t, uint hash)
{
    checkTable(t);
    return hash & (getTableSize(t) - 1);
}

static uint getSlotHash(const uint *t, uint slot)
{
    checkTable(t);
    return t[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_HASH];
}

static stringref getSlotValue(const uint *t, uint slot)
{
    checkSlot(t, slot);
    return refFromUint(t[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE]);
}

static boolean isSlotEmpty(const uint *t, uint slot)
{
    checkTable(t);
    return getSlotValue(t, slot) ? false : true;
}

static boolean slotContainsString(const uint *t, uint slot, uint hash,
                                  const char *string, size_t length)
{
    checkTable(t);
    return getSlotHash(t, slot) == hash &&
        StringPoolGetStringLength(getSlotValue(t, slot)) == length &&
        memcmp(StringPoolGetString(getSlotValue(t, slot)), string, length) == 0;
}

void StringPoolInit(void)
{
    assert(!stringData);
    assert(!table);
    stringData = (char*)malloc(65536);
    table = (uint*)calloc(1024 + TABLE_DATA_BEGIN, sizeof(uint));
    table[TABLE_SIZE] = 1024;
}

void StringPoolDispose(void)
{
    free(stringData);
    free(table);
}

stringref StringPoolAdd(const char *token)
{
    assert(token != null);
    return StringPoolAdd2(token, strlen(token));
}

stringref StringPoolAdd2(const char *token, size_t length)
{
    uint hash;
    uint slot;
    stringref ref;
    uint *cachedTable = table;

    assert(token);
    assert(stringData);
    assert(cachedTable);
    assert(length <= 127); /* TODO: Increase string length limit */

    hash = UtilHashString(token, length);
    slot = getSlotForHash(cachedTable, hash);
    while (!isSlotEmpty(cachedTable, slot))
    {
        if (slotContainsString(cachedTable, slot, hash, token, length))
        {
            return getSlotValue(cachedTable, slot);
        }
        slot++;
        if (slot == getTableSize(cachedTable))
        {
            slot = 0;
        }
    }
    stringData[dataSize++] = (char)length;
    ref = refFromSize(dataSize);
    memcpy(&stringData[dataSize], token, length);
    stringData[dataSize + length] = 0;
    table[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = uintFromRef(ref);
    table[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_HASH] = hash;
    dataSize += length + 1;
    return ref;
}

const char *StringPoolGetString(stringref ref)
{
    assert(stringData);
    assert(ref);
    assert((size_t)ref < dataSize);
    return &stringData[sizeFromRef(ref)];
}

size_t StringPoolGetStringLength(stringref ref)
{
    assert(stringData);
    assert(ref);
    assert((size_t)ref < dataSize);
    return (size_t)stringData[sizeFromRef(ref) - 1];
}
