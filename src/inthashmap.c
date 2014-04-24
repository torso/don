#include <string.h>
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

static int getSlotKey(const inthashmap *map, size_t slot)
{
    checkSlot(map, slot);
    return map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY];
}

static int getSlotValue(const inthashmap *map, size_t slot)
{
    checkSlot(map, slot);
    return map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE];
}

static bool isSlotEmpty(const inthashmap *map, size_t slot)
{
    checkSlot(map, slot);
    return !getSlotKey(map, slot);
}

static void setSlot(inthashmap *map, size_t slot, int key, int value)
{
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_KEY] = key;
    map->table[slot * TABLE_ENTRY_SIZE + TABLE_ENTRY_VALUE] = value;
}

static size_t getSlotForKey(const inthashmap *map, int key)
{
    assert(key);
    return (uint)key & (map->tableSize - 1);
}

static size_t findSlot(const inthashmap *map, int key)
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

static void addEntry(inthashmap *map, int key, int value)
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
    int *oldTable;
    int *restrict p;
    int *restrict limit;

    if (map->size > map->growLimit)
    {
        oldTable = map->table;
        map->tableSize *= 2;
        map->size = 0;
        map->growLimit = map->tableSize * 3 / 4;
        map->table = (int*)calloc(map->tableSize * 2, sizeof(int));
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
    map->table = (int*)calloc(map->tableSize * 2, sizeof(int));
}

void IntHashMapDispose(inthashmap *map)
{
    free(map->table);
}

void IntHashMapClear(inthashmap *map)
{
    if (map->size)
    {
        map->size = 0;
        memset(map->table, 0, map->tableSize * 2 * sizeof(int));
    }
}

void IntHashMapAdd(inthashmap *map, int key, int value)
{
    assert(key);
    grow(map);
    addEntry(map, key, value);
}

int IntHashMapGet(const inthashmap *map, int key)
{
    return getSlotValue(map, findSlot(map, key));
}

void IntHashMapRemove(inthashmap *map, int key)
{
    int value;
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

int IntHashMapSet(inthashmap *map, int key, int value)
{
    size_t slot = findSlot(map, key);
    if (isSlotEmpty(map, slot))
    {
        map->size++;
        setSlot(map, slot, key, value);
        grow(map);
        return 0;
    }
    else
    {
        int old = getSlotValue(map, slot);
        setSlot(map, slot, key, value);
        return old;
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

bool IntHashMapIteratorNext(inthashmapiterator *iterator, int *key, int *value)
{
    int slotKey;

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
