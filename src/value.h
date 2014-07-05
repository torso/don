#define DEBUG_FUTURE 0

#define TYPE_FLAG_FUTURE 0x100

typedef enum
{
    TYPE_INVALID               = 0,
    TYPE_VALUE                 = 1,
    TYPE_NULL                  = 2,
    TYPE_BOOLEAN_TRUE          = 3,
    TYPE_BOOLEAN_FALSE         = 4,
    TYPE_INTEGER               = 5,
    TYPE_STRING                = 6,
    TYPE_STRING_WRAPPED        = 7,
    TYPE_SUBSTRING             = 8,
    TYPE_FILE                  = 9,
    TYPE_ARRAY                 = 10,
    TYPE_INTEGER_RANGE         = 11,
    TYPE_CONCAT_LIST           = 12,
    TYPE_FUTURE                = 13 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_NOT            = 14 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_NEG            = 15 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_INV            = 16 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_EQUALS         = 17 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_NOT_EQUALS     = 18 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_LESS           = 19 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_LESS_EQUALS    = 20 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_AND            = 21 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_ADD            = 22 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_SUB            = 23 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_MUL            = 24 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_DIV            = 25 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_REM            = 26 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_VALID_INDEX    = 27 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_INDEXED_ACCESS = 28 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_RANGE          = 29 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_CONCAT         = 30 | TYPE_FLAG_FUTURE,
    TYPE_FUTURE_CONCAT_STRING  = 31 | TYPE_FLAG_FUTURE
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


extern bool VWait(vref *value);

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


extern vref VEquals(VM *vm, vref value1, vref value2);
extern vref VNotEquals(VM *vm, vref value1, vref value2);
extern vref VLess(VM *vm, vref value1, vref value2);
extern vref VLessEquals(VM *vm, vref value1, vref value2);
extern vref VAdd(VM *vm, vref value1, vref value2);
extern vref VSub(VM *vm, vref value1, vref value2);
extern vref VMul(VM *vm, vref value1, vref value2);
extern vref VDiv(VM *vm, vref value1, vref value2);
extern vref VRem(VM *vm, vref value1, vref value2);
extern vref VAnd(VM *vm, vref value1, vref value2);
extern vref VNot(VM *vm, vref value);
extern vref VNeg(VM *vm, vref value);
extern vref VInv(VM *vm, vref value);
extern vref VValidIndex(VM *vm, vref collection, vref index);
extern vref VIndexedAccess(VM *vm, vref value1, vref value2);
extern vref VRange(VM *vm, vref value1, vref value2);
extern vref VConcat(VM *vm, vref value1, vref value2);
extern vref VConcatString(VM *vm, size_t count, vref *values);


extern pureconst bool VIsStringType(VType type);

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
