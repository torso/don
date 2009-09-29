#include <stdlib.h>
#include <string.h>
#include "builder.h"
#include "stringpool.h"

#define TABLE_SIZE 0
#define TABLE_DATA_BEGIN 2

#define TABLE_ENTRY_HASH 0
#define TABLE_ENTRY_VALUE 1
#define TABLE_ENTRY_SIZE 2

static char* stringData = null;
static uint dataSize = 0;
static uint* table = null;
/*static uint stringCount = 0;*/

static void checkTable(const uint* t)
{
    assert(t);
    assert(t[TABLE_SIZE] > TABLE_DATA_BEGIN);
    assert((t[TABLE_SIZE] - TABLE_DATA_BEGIN) % TABLE_ENTRY_SIZE == 0);
}

static uint getTableSize(const uint* t)
{
    checkTable(t);
    return (t[TABLE_SIZE] - TABLE_DATA_BEGIN) / TABLE_ENTRY_SIZE;
}

static void checkSlot(const uint* t, uint slot)
{
    assert(getTableSize(t) > slot);
}

static uint getSlotForHash(const uint* t, uint hash)
{
    checkTable(t);
    return hash & (getTableSize(t) - 1);
}

static uint getSlotHash(const uint* t, uint slot)
{
    checkTable(t);
    return t[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_HASH];
}

static uint getSlotValue(const uint* t, uint slot)
{
    checkSlot(t, slot);
    return t[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE];
}

static boolean isSlotEmpty(const uint* t, uint slot)
{
    checkTable(t);
    return getSlotValue(t, slot) ? false : true;
}

static boolean slotContainsString(const uint* t, uint slot, uint hash,
                                  const char* string, uint length)
{
    checkTable(t);
    return getSlotHash(t, slot) == hash &&
        StringPoolGetStringLength(getSlotValue(t, slot)) == length &&
        memcmp(&stringData[getSlotValue(t, slot)],
               string, length) == 0;
}

void StringPoolInit()
{
    assert(!stringData);
    assert(!table);
    stringData = malloc(65536);
    assert(stringData); /* TODO: handle oom */
    table = zmalloc((1024 + TABLE_DATA_BEGIN) * sizeof(uint));
    assert(table); /* TODO: handle oom */
    table[TABLE_SIZE] = 1024;
}

void StringPoolFree()
{
    free(stringData);
    free(table);
}

stringref StringPoolAdd(const char* token)
{
    assert(token != null);
    return StringPoolAdd2(token, strlen(token));
}

stringref StringPoolAdd2(const char* token, uint length)
{
    uint i;
    uint hash;
    uint slot;
    stringref ref;
    uint* cachedTable = table;

    assert(token);
    assert(stringData);
    assert(cachedTable);
    assert(length <= 127); /* TODO: Increase string length limit */

    hash = 1;
    for (i = 0; i < length; i++)
    {
        assert(token[i]);
        hash = 31 * hash + token[i];
    }
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
    ref = dataSize;
    memcpy(&stringData[dataSize], token, length);
    stringData[dataSize + length] = 0;
    table[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = ref;
    table[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_HASH] = hash;
    dataSize += length + 1;
    return ref;
}

const char* StringPoolGetString(stringref ref)
{
    assert(stringData);
    assert(ref > 0);
    assert(ref < dataSize);
    return &stringData[ref];
}

uint StringPoolGetStringLength(stringref ref)
{
    assert(stringData);
    assert(ref > 0);
    assert(ref < dataSize);
    return stringData[ref - 1];
}
