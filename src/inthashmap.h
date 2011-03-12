#define INTHASHMAP_H

struct inthashmap
{
    uint *table;
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
extern nonnull void IntHashMapAdd(inthashmap *map, uint key, uint value);
extern nonnull uint IntHashMapGet(const inthashmap *map, uint key);
extern nonnull void IntHashMapSet(inthashmap *map, uint key, uint value);
extern nonnull void IntHashMapRemove(inthashmap *map, uint key);
extern nonnull pure size_t IntHashMapSize(const inthashmap *map);

extern nonnull void IntHashMapIteratorInit(const inthashmap *map,
                                           inthashmapiterator *iterator);
extern nonnull boolean IntHashMapIteratorNext(inthashmapiterator *iterator,
                                              uint *key, uint *value);
