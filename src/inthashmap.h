#define INTHASHMAP_H

typedef struct
{
    uint tableSize;
    uint *table;
} inthashmap;

extern boolean IntHashMapInit(inthashmap *map, uint capacity);
extern void IntHashMapDispose(inthashmap *map);
extern void IntHashMapAdd(inthashmap *map, uint key, uint value);
extern uint IntHashMapGet(const inthashmap *map, uint key);
