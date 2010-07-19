#include <stdlib.h>
#include "builder.h"
#include "inthashmap.h"
#include "math.h"

#define TABLE_ENTRY_KEY 0
#define TABLE_ENTRY_VALUE 1
#define TABLE_ENTRY_SIZE 2

static void checkSlot(const inthashmap *map, uint slot)
{
    assert(map->tableSize > slot);
}

static uint getSlotForKey(const inthashmap *map, uint key)
{
    return key & (map->tableSize - 1);
}

static uint getSlotKey(const inthashmap *map, uint slot)
{
    checkSlot(map, slot);
    return map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY];
}

static uint getSlotValue(const inthashmap *map, uint slot)
{
    checkSlot(map, slot);
    return map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE];
}

static boolean isSlotEmpty(const inthashmap *map, uint slot)
{
    checkSlot(map, slot);
    return getSlotKey(map, slot) ? false : true;
}

boolean IntHashMapInit(inthashmap *map, uint capacity)
{
    map->tableSize = max(roundToPow2(capacity * 4 / 3), capacity + 1);
    map->table = (uint*)zmalloc(map->tableSize * 2 * sizeof(uint));
    return map->table ? true : false;
}

void IntHashMapDispose(inthashmap *map)
{
    free(map->table);
}

void IntHashMapAdd(inthashmap *map, uint key, uint value)
{
    uint slot = getSlotForKey(map, key);
    while (!isSlotEmpty(map, slot))
    {
        assert(getSlotKey(map, slot) != key);
        slot++;
        if (slot == map->tableSize)
        {
            slot = 0;
        }
    }
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY] = key;
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = value;
}

uint IntHashMapGet(const inthashmap *map, uint key)
{
    uint slot = getSlotForKey(map, key);
    for (;;)
    {
        if (getSlotKey(map, slot) == key)
        {
            return getSlotValue(map, slot);
        }
        if (isSlotEmpty(map, slot))
        {
            break;
        }
        slot++;
        if (slot == map->tableSize)
        {
            slot = 0;
        }
    }
    return 0;
}
