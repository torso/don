#define DEBUG_FUTURE 0

#define TYPE_FLAG_FUTURE 0x80

typedef enum
{
    TYPE_INVALID = 0,
    TYPE_VALUE,
    TYPE_NULL,
    TYPE_BOOLEAN_TRUE,
    TYPE_BOOLEAN_FALSE,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_SUBSTRING,
    TYPE_FILE,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE,
    TYPE_CONCAT_LIST,
    TYPE_FUTURE = TYPE_FLAG_FUTURE,
    TYPE_FUTURE_NOT,
    TYPE_FUTURE_NEG,
    TYPE_FUTURE_INV,
    TYPE_FUTURE_EQUALS,
    TYPE_FUTURE_NOT_EQUALS,
    TYPE_FUTURE_LESS,
    TYPE_FUTURE_LESS_EQUALS,
    TYPE_FUTURE_AND,
    TYPE_FUTURE_ADD,
    TYPE_FUTURE_SUB,
    TYPE_FUTURE_MUL,
    TYPE_FUTURE_DIV,
    TYPE_FUTURE_REM,
    TYPE_FUTURE_VALID_INDEX,
    TYPE_FUTURE_INDEXED_ACCESS,
    TYPE_FUTURE_RANGE,
    TYPE_FUTURE_CONCAT,
    TYPE_FUTURE_CONCAT_STRING
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
extern vref VNull;
extern vref VTrue;
extern vref VFalse;
extern vref VEmptyString;
extern vref VEmptyList;
extern vref VNewline;


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


extern pureconst bool VIsInteger(vref object);
extern vref VBoxInteger(int value);
extern vref VBoxUint(uint value);
extern vref VBoxSize(size_t value);
extern int VUnboxInteger(vref object);
extern size_t VUnboxSize(vref object);


extern pureconst bool VIsStringType(VType type);

extern nonnull bool VIsString(vref object);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern size_t VStringLength(vref value);

extern nonnull vref VCreateString(const char *string, size_t length);
extern nonnull vref VCreateUninitialisedString(size_t length, char **data);
extern nonnull vref VCreateSubstring(vref string, size_t offset, size_t length);
extern nonnull vref VCreateStringFormatted(const char *format, va_list ap);

/*
  Returns a null-terminated string. This function must only be used when the
  string was added through the string pool. (This should be true for all code
  running before the VM starts.)
*/
extern nonnull const char *VGetString(vref object);
extern nonnull char *VGetStringCopy(vref object);

/*
  Converts the object to a string and writes it to dst. The written string will
  be exactly as long as HeapStringLength returned. The string will not be zero
  terminated. A pointer just past the end of the written string is returned.
*/
extern nonnull char *VWriteString(vref value, char *dst);
extern nonnull char *VWriteSubstring(vref object, size_t offset,
                                     size_t length, char *dst);

extern nonnull vref VStringIndexOf(vref text, size_t startOffset, vref substring);


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
