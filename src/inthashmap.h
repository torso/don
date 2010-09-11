#define INTHASHMAP_H

struct inthashmap
{
    uint *table;
    uint tableSize;
    uint size;
};

typedef struct
{
    const inthashmap *map;
    uint position;
} inthashmapiterator;

extern nonnull ErrorCode IntHashMapInit(inthashmap *map, uint capacity);
extern nonnull void IntHashMapDispose(inthashmap *map);
extern nonnull ErrorCode IntHashMapAdd(inthashmap *map, uint key, uint value);
extern nonnull pure uint IntHashMapGet(const inthashmap *map, uint key);
extern nonnull pure uint IntHashMapSize(const inthashmap *map);

extern nonnull void IntHashMapIteratorInit(const inthashmap *map,
                                           inthashmapiterator *iterator);
extern nonnull boolean IntHashMapIteratorNext(inthashmapiterator *iterator,
                                              uint *key, uint *value);
