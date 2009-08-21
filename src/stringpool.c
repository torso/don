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

static uint getTableSize(const uint* t)
{
    return t[TABLE_SIZE] / TABLE_ENTRY_SIZE;
}

static uint getSlotForHash(const uint* t, uint hash)
{
    return (hash % getTableSize(t)) * TABLE_ENTRY_SIZE;
}

static uint getSlotHash(const uint* t, uint slot)
{
    return t[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_HASH];
}

static uint getSlotValue(const uint* t, uint slot)
{
    return t[TABLE_DATA_BEGIN + slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE];
}

static boolean isSlotEmpty(const uint* t, uint slot)
{
    return getSlotValue(t, slot) ? false : true;
}

static boolean slotContainsString(const uint* t, uint slot, uint hash,
                                  const char* string, uint length)
{
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
    table = zmalloc(1024 + TABLE_DATA_BEGIN);
    assert(table); /* TODO: handle oom */
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
    while (isSlotEmpty(cachedTable, slot))
    {
        if (slotContainsString(cachedTable, slot, hash, token, length))
        {
            return getSlotValue(cachedTable, slot);
        }
        slot += TABLE_ENTRY_SIZE;
        if (slot == getTableSize(cachedTable))
        {
            slot = 0;
        }
    }
    stringData[dataSize++] = (char)length;
    ref = dataSize;
    memcpy(stringData, token, length + 1);
    table[slot + TABLE_ENTRY_VALUE] = ref;
    table[slot + TABLE_ENTRY_HASH] = hash;
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
