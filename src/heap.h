#include "instruction.h"
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

/*
  These values may be reused freely, so that new objects doesn't have to be
  allocated. Never compare to these values. Use the functions in value.h, as
  additional processing may be needed to determine the value of a vref.
*/
extern vref HeapTrue;
extern vref HeapFalse;
extern vref HeapEmptyString;
extern vref HeapEmptyList;
extern vref HeapNewline;
extern vref HeapInvalid;


extern void HeapInit(void);
extern void HeapDispose(void);

extern nonnull void HeapGet(vref v, HeapObject *ho);
extern nonnull char *HeapDebug(vref object, boolean address);
extern nonnull VType HeapGetObjectType(vref object);
extern nonnull size_t HeapGetObjectSize(vref object);
extern nonnull const byte *HeapGetObjectData(vref object);
extern nonnull void HeapHash(vref object, HashState *hash);
extern nonnull boolean HeapEquals(vref object1, vref object2);
extern nonnull int HeapCompare(vref object1, vref object2);



extern nonnull vref HeapBoxInteger(int value);
extern nonnull vref HeapBoxUint(uint value);
extern nonnull vref HeapBoxSize(size_t value);
extern nonnull int HeapUnboxInteger(vref object);
extern nonnull size_t HeapUnboxSize(vref object);
extern nonnull int HeapIntegerSign(vref object);



extern nonnull vref HeapCreateString(const char *string, size_t length);
extern nonnull vref HeapCreateUninitialisedString(size_t length,
                                                  char **data);
extern nonnull vref HeapCreateWrappedString(const char *string,
                                            size_t length);
extern nonnull vref HeapCreateSubstring(vref string, size_t offset,
                                        size_t length);
extern nonnull vref HeapCreateStringFormatted(const char *format, va_list ap);
extern nonnull boolean HeapIsString(vref object);

/*
  Returns a null-terminated string. This function must only be used when the
  string was added through the string pool. (This should be true for all code
  running before the VM starts.)
*/
extern nonnull const char *HeapGetString(vref object);

extern nonnull char *HeapWriteSubstring(vref object, size_t offset,
                                        size_t length, char *restrict dst);

extern nonnull vref HeapStringIndexOf(vref text, size_t startOffset,
                                      vref substring);



/*
  Creates a path object, if the supplied object isn't one already.
*/
extern nonnull vref HeapCreatePath(vref path);
extern nonnull const char *HeapGetPath(vref path, size_t *length);
extern nonnull boolean HeapIsFile(vref object);
extern nonnull vref HeapPathFromParts(vref path, vref name, vref extension);

extern nonnull vref HeapCreateFilelist(vref value);
extern nonnull vref HeapCreateFilelistGlob(const char *pattern, size_t length);



extern nonnull vref HeapCreateRange(vref lowObject,
                                    vref highObject);
extern nonnull boolean HeapIsRange(vref object);
extern nonnull vref HeapRangeLow(vref range);
extern nonnull vref HeapRangeHigh(vref range);



extern nonnull vref HeapSplit(vref string, vref delimiter,
                              boolean removeEmpty,
                              boolean trimLastIfEmpty);

extern nonnull vref *HeapCreateArray(size_t size);
extern nonnull vref HeapFinishArray(vref *array);
extern nonnull vref HeapCreateArrayFromData(const vref *values, size_t size);
extern nonnull vref HeapCreateArrayFromVector(const intvector *values);
extern nonnull vref HeapCreateArrayFromVectorSegment(const intvector *values,
                                                     size_t start, size_t length);
extern nonnull vref HeapConcatList(vref list1, vref list2);
extern nonnull boolean HeapIsCollection(vref object);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull boolean HeapCollectionGet(vref object,
                                         vref indexObject,
                                         vref *value);



extern boolean HeapIsFutureValue(vref object);
extern vref HeapCreateFutureValue(void);
extern void HeapSetFutureValue(vref object, vref value);
extern vref HeapTryWait(vref object);
extern vref HeapWait(vref object);



extern vref HeapApplyUnary(Instruction op, vref value);
extern vref HeapApplyBinary(Instruction op,
                            vref value1, vref value2);
