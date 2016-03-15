#define INTHASHMAP_H

struct inthashmap
{
    int *table;
    size_t tableSize;
    size_t size;
    size_t growLimit;
};

typedef struct
{
    const inthashmap *map;
    size_t position;
} inthashmapiterator;

nonnull void IntHashMapInit(inthashmap *map, size_t capacity);
nonnull void IntHashMapDispose(inthashmap *map);
nonnull void IntHashMapClear(inthashmap *map);
nonnull void IntHashMapAdd(inthashmap *map, int key, int value);
#define IntHashMapAddUint(map, key, value) (IntHashMapAdd((map), (key), (int)(value)))
nonnull int IntHashMapGet(const inthashmap *map, int key);
#define IntHashMapGetUint(map, key) ((uint)IntHashMapGet((map), (key)))
nonnull int IntHashMapSet(inthashmap *map, int key, int value);
#define IntHashMapSetUint(map, key, value) ((uint)IntHashMapSet((map), (key), (int)(value)))
nonnull void IntHashMapRemove(inthashmap *map, int key);
nonnull pure size_t IntHashMapSize(const inthashmap *map);

nonnull void IntHashMapIteratorInit(const inthashmap *map, inthashmapiterator *iterator);
nonnull bool IntHashMapIteratorNext(inthashmapiterator *iterator, int *key, int *value);
