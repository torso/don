#include "builder.h"
#include "inthashmap.h"
#include "math.h"

#define TABLE_ENTRY_KEY 0
#define TABLE_ENTRY_VALUE 1
#define TABLE_ENTRY_SIZE 2

static void checkSlot(const inthashmap *map, size_t slot)
{
    assert(map->tableSize > slot);
}

static size_t getSlotForKey(const inthashmap *map, uint key)
{
    return key & (map->tableSize - 1);
}

static uint getSlotKey(const inthashmap *map, size_t slot)
{
    checkSlot(map, slot);
    return map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY];
}

static uint getSlotValue(const inthashmap *map, size_t slot)
{
    checkSlot(map, slot);
    return map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE];
}

static boolean isSlotEmpty(const inthashmap *map, size_t slot)
{
    checkSlot(map, slot);
    return !getSlotKey(map, slot);
}

ErrorCode IntHashMapInit(inthashmap *map, size_t capacity)
{
    map->tableSize = max(roundSizeToPow2(capacity * 4 / 3), capacity + 1);
    map->size = 0;
    map->table = (uint*)calloc(map->tableSize * 2, sizeof(uint));
    return map->table ? NO_ERROR : OUT_OF_MEMORY;
}

void IntHashMapDispose(inthashmap *map)
{
    free(map->table);
}

ErrorCode IntHashMapAdd(inthashmap *map, uint key, uint value)
{
    size_t slot = getSlotForKey(map, key);
    assert(key);
    assert(map->size < map->tableSize);
    while (!isSlotEmpty(map, slot))
    {
        assert(getSlotKey(map, slot) != key);
        slot++;
        if (slot == map->tableSize)
        {
            slot = 0;
        }
    }
    map->size++;
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY] = key;
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = value;
    return NO_ERROR;
}

uint IntHashMapGet(const inthashmap *map, uint key)
{
    size_t slot = getSlotForKey(map, key);
    assert(key);
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

size_t IntHashMapSize(const inthashmap *map)
{
    return map->size;
}

void IntHashMapIteratorInit(const inthashmap *map, inthashmapiterator *iterator)
{
    iterator->map = map;
    iterator->position = 0;
}

boolean IntHashMapIteratorNext(inthashmapiterator *iterator,
                               uint *key, uint *value)
{
    uint slotKey;

    for (;; iterator->position++)
    {
        if (iterator->position >= iterator->map->tableSize)
        {
            return false;
        }
        slotKey = getSlotKey(iterator->map, iterator->position);
        if (slotKey)
        {
            *key = slotKey;
            *value = getSlotValue(iterator->map, iterator->position);
            iterator->position++;
            return true;
        }
    }
}
