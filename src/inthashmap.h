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

extern nonnull void IntHashMapInit(inthashmap *map, size_t capacity);
extern nonnull void IntHashMapDispose(inthashmap *map);
extern nonnull void IntHashMapClear(inthashmap *map);
extern nonnull void IntHashMapAdd(inthashmap *map, int key, int value);
#define IntHashMapAddUint(map, key, value) (IntHashMapAdd((map), (key), (int)(value)))
extern nonnull int IntHashMapGet(const inthashmap *map, int key);
#define IntHashMapGetUint(map, key) ((uint)IntHashMapGet((map), (key)))
extern nonnull int IntHashMapSet(inthashmap *map, int key, int value);
#define IntHashMapSetUint(map, key, value) ((uint)IntHashMapSet((map), (key), (int)(value)))
extern nonnull void IntHashMapRemove(inthashmap *map, int key);
extern nonnull pure size_t IntHashMapSize(const inthashmap *map);

extern nonnull void IntHashMapIteratorInit(const inthashmap *map,
                                           inthashmapiterator *iterator);
extern nonnull boolean IntHashMapIteratorNext(inthashmapiterator *iterator,
                                              int *key, int *value);
