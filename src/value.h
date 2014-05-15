typedef enum
{
    TYPE_NULL,
    TYPE_BOOLEAN_TRUE,
    TYPE_BOOLEAN_FALSE,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_STRING_WRAPPED,
    TYPE_SUBSTRING,
    TYPE_FILE,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE,
    TYPE_CONCAT_LIST,
    TYPE_FUTURE,
    TYPE_INVALID
} VType;

typedef enum
{
    TRUTHY,
    FALSY,
    FUTURE /* Value not yet known. */
} VBool;

/*
  These values may be reused freely, so that new objects doesn't have to be
  allocated. Never compare to these values. Use the functions in value.h, as
  additional processing may be needed to determine the value of a vref.
*/
extern vref HeapNull;
extern vref HeapTrue;
extern vref HeapFalse;
extern vref HeapEmptyString;
extern vref HeapEmptyList;
extern vref HeapNewline;


extern VBool VGetBool(vref value);

/*
  Returns true if the value is truthy.
  Returns false if true value if falsy or not yet known.
*/
extern bool VIsTruthy(vref value);

/*
  Returns true if the value is falsy.
  Returns false if true value if truthy or not yet known.
*/
extern bool VIsFalsy(vref value);


/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern size_t VStringLength(vref value);

/*
  Converts the object to a string and writes it to dst. The written string will
  be exactly as long as HeapStringLength returned. The string will not be zero
  terminated. A pointer just past the end of the written string is returned.
*/
extern nonnull char *VWriteString(vref value, char *dst);


extern nonnull vref *VCreateArray(size_t size);
extern nonnull vref VFinishArray(vref *array);
extern nonnull vref VCreateArrayFromData(const vref *values, size_t size);
extern nonnull vref VCreateArrayFromVector(const intvector *values);
extern nonnull vref VCreateArrayFromVectorSegment(const intvector *values,
                                                  size_t start, size_t length);
extern nonnull vref VConcatList(vref list1, vref list2);
extern pureconst bool VIsCollectionType(VType type);
extern nonnull bool VIsCollection(vref object);
extern size_t VCollectionSize(vref value);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull bool VCollectionGet(vref object, vref indexObject, vref *value);
