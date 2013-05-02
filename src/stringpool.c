#include <string.h>
#include "common.h"
#include "instruction.h"
#include "heap.h"
#include "stringpool.h"
#include "util.h"

#define TABLE_SIZE 0
#define TABLE_DATA_BEGIN 2

#define TABLE_ENTRY_HASH 0
#define TABLE_ENTRY_VALUE 1
#define TABLE_ENTRY_SIZE 2

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

static vref getSlotValue(const uint *t, uint slot)
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
        HeapStringLength(getSlotValue(t, slot)) == length &&
        memcmp(HeapGetString(getSlotValue(t, slot)), string, length) == 0;
}

void StringPoolInit(void)
{
    assert(!table);
    table = (uint*)calloc(1024 + TABLE_DATA_BEGIN, sizeof(uint));
    table[TABLE_SIZE] = 1024;
}

void StringPoolDispose(void)
{
    free(table);
}

vref StringPoolAdd(const char *token)
{
    assert(token != null);
    return StringPoolAdd2(token, strlen(token));
}

vref StringPoolAdd2(const char *token, size_t length)
{
    uint hash;
    uint slot;
    vref ref;
    uint *cachedTable = table;

    assert(token);
    assert(cachedTable);
    assert(length <= 65535);

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
    ref = HeapCreateString(token, length);
    table[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = uintFromRef(ref);
    table[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_HASH] = hash;
    return ref;
}
