#include "common.h"
#include "inthashmap.h"
#include "math.h"

#define TABLE_ENTRY_KEY 0
#define TABLE_ENTRY_VALUE 1
#define TABLE_ENTRY_SIZE 2

static void checkSlot(const inthashmap *map, size_t slot)
{
    assert(map->tableSize > slot);
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

static void setSlot(inthashmap *map, size_t slot, uint key, uint value)
{
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY] = key;
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = value;
}

static size_t getSlotForKey(const inthashmap *map, uint key)
{
    assert(key);
    return key & (map->tableSize - 1);
}

static size_t findSlot(const inthashmap *map, uint key)
{
    size_t slot = getSlotForKey(map, key);
    for (;;)
    {
        if (getSlotKey(map, slot) == key || isSlotEmpty(map, slot))
        {
            return slot;
        }
        slot++;
        if (slot == map->tableSize)
        {
            slot = 0;
        }
    }
}

static void addEntry(inthashmap *map, uint key, uint value)
{
    size_t slot = getSlotForKey(map, key);

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
    setSlot(map, slot, key, value);
}

static void grow(inthashmap *map)
{
    uint *oldTable;
    uint *restrict p;
    uint *restrict limit;

    if (map->size > map->growLimit)
    {
        oldTable = map->table;
        map->tableSize *= 2;
        map->size = 0;
        map->growLimit = map->tableSize * 3 / 4;
        map->table = (uint*)calloc(map->tableSize * 2, sizeof(uint));
        limit = oldTable + map->tableSize;
        for (p = oldTable; p < limit; p += 2)
        {
            if (*p)
            {
                addEntry(map, *p, *(p + 1));
            }
        }
        free(oldTable);
    }
}


void IntHashMapInit(inthashmap *map, size_t capacity)
{
    map->tableSize = max(roundSizeToPow2(capacity * 4 / 3), capacity + 1);
    map->growLimit = map->tableSize * 3 / 4;
    map->size = 0;
    map->table = (uint*)calloc(map->tableSize * 2, sizeof(uint));
}

void IntHashMapDispose(inthashmap *map)
{
    free(map->table);
}

void IntHashMapAdd(inthashmap *map, uint key, uint value)
{
    assert(key);
    grow(map);
    addEntry(map, key, value);
}

uint IntHashMapGet(const inthashmap *map, uint key)
{
    return getSlotValue(map, findSlot(map, key));
}

void IntHashMapRemove(inthashmap *map, uint key)
{
    uint value;
    size_t slot = findSlot(map, key);
    if (!isSlotEmpty(map, slot))
    {
        map->size--;
        setSlot(map, slot, 0, 0);
        for (;;)
        {
            slot++;
            if (slot == map->tableSize)
            {
                slot = 0;
            }
            if (isSlotEmpty(map, slot))
            {
                break;
            }
            key = getSlotKey(map, slot);
            value = getSlotValue(map, slot);
            map->size--;
            setSlot(map, slot, 0, 0);
            IntHashMapAdd(map, key, value);
        }
    }
}

void IntHashMapSet(inthashmap *map, uint key, uint value)
{
    size_t slot = findSlot(map, key);
    if (isSlotEmpty(map, slot))
    {
        map->size++;
        setSlot(map, slot, key, value);
        grow(map);
    }
    else
    {
        setSlot(map, slot, key, value);
    }
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
