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


extern void HeapInit(void);
extern void HeapDispose(void);

extern byte *HeapAlloc(VType type, size_t size);
extern nonnull vref HeapFinishAlloc(byte *objectData);
extern nonnull vref HeapFinishRealloc(byte *objectData, size_t size);

extern nonnull void HeapGet(vref v, HeapObject *ho);
extern nonnull char *HeapDebug(vref object);
extern nonnull VType HeapGetObjectType(vref object);
extern nonnull size_t HeapGetObjectSize(vref object);
extern nonnull const byte *HeapGetObjectData(vref object);
extern nonnull void HeapHash(vref object, HashState *hash);


/*
  Creates a path object, if the supplied object isn't one already.
*/
extern nonnull vref HeapCreatePath(vref path);
extern nonnull const char *HeapGetPath(vref path, size_t *length);
extern nonnull bool HeapIsFile(vref object);
extern nonnull vref HeapPathFromParts(vref path, vref name, vref extension);

extern nonnull vref HeapCreateFilelist(vref value);
extern nonnull vref HeapCreateFilelistGlob(const char *pattern, size_t length);



extern nonnull vref HeapCreateRange(vref lowObject, vref highObject);
extern nonnull bool HeapIsRange(vref object);
extern nonnull vref HeapRangeLow(vref range);
extern nonnull vref HeapRangeHigh(vref range);



extern nonnull vref HeapSplit(vref string, vref delimiter, bool removeEmpty, bool trimLastIfEmpty);



extern bool HeapIsFutureValue(vref object);
extern vref HeapCreateFutureValue(void);
extern void HeapSetFutureValue(vref object, vref value);
