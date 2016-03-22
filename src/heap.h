#include "value.h"

typedef struct
{
    VType type;
    size_t size;
    byte *data;
} HeapObject;

/* TODO: Move to value.c */
typedef struct
{
    vref string;
    size_t offset;
    size_t length;
} SubString;


void HeapInit(void);
void HeapDispose(void);

byte *HeapAlloc(VType type, size_t size);
nonnull vref HeapFinishAlloc(byte *objectData);
nonnull vref HeapFinishRealloc(byte *objectData, size_t size);
nonnull void HeapAllocAbort(byte *objectData);
void HeapFree(vref value);

vref HeapTop(void); /* TODO: Deprecated */
vref HeapNext(vref object); /* TODO: Deprecated */

nonnull void HeapGet(vref v, HeapObject *ho);
nonnull char *HeapDebug(vref object);
nonnull VType HeapGetObjectType(vref object);
nonnull size_t HeapGetObjectSize(vref object);
nonnull const byte *HeapGetObjectData(vref object);
nonnull void HeapHash(vref object, HashState *hash);



nonnull vref HeapCreateRange(vref lowObject, vref highObject);
nonnull bool HeapIsRange(vref object);
nonnull vref HeapRangeLow(vref range);
nonnull vref HeapRangeHigh(vref range);



nonnull vref HeapSplit(vref string, vref delimiter, bool removeEmpty, bool trimLastIfEmpty);
