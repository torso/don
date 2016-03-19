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

nonnull void HeapGet(vref v, HeapObject *ho);
nonnull char *HeapDebug(vref object);
nonnull VType HeapGetObjectType(vref object);
nonnull size_t HeapGetObjectSize(vref object);
nonnull const byte *HeapGetObjectData(vref object);
nonnull void HeapHash(vref object, HashState *hash);


/*
  Creates a path object, if the supplied object isn't one already.
*/
nonnull vref HeapCreatePath(vref path);
nonnull const char *HeapGetPath(vref path, size_t *length);
nonnull bool HeapIsFile(vref object);
nonnull vref HeapPathFromParts(vref path, vref name, vref extension);

nonnull vref HeapCreateFilelist(vref value);
nonnull vref HeapCreateFilelistGlob(const char *pattern, size_t length);



nonnull vref HeapCreateRange(vref lowObject, vref highObject);
nonnull bool HeapIsRange(vref object);
nonnull vref HeapRangeLow(vref range);
nonnull vref HeapRangeHigh(vref range);



nonnull vref HeapSplit(vref string, vref delimiter, bool removeEmpty, bool trimLastIfEmpty);



bool HeapIsFutureValue(vref object);
vref HeapCreateFutureValue(void);
