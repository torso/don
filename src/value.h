typedef enum
{
    TYPE_INVALID = 0,
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
    TYPE_FUTURE
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
extern vref VFuture;


bool VWait(vref *value);

VBool VGetBool(vref value);

/*
  Returns true if the value is truthy.
  Returns false if true value if falsy or not yet known.
*/
bool VIsTruthy(vref value);

/*
  Returns true if the value is falsy.
  Returns false if true value if truthy or not yet known.
*/
bool VIsFalsy(vref value);


pureconst bool VIsInteger(vref object);
vref VBoxInteger(int value);
vref VBoxUint(uint value);
vref VBoxSize(size_t value);
int VUnboxInteger(vref object);
size_t VUnboxSize(vref object);


pureconst bool VIsStringType(VType type);

nonnull bool VIsString(vref object);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
size_t VStringLength(vref value);

nonnull vref VCreateString(const char *string, size_t length);
nonnull vref VCreateUninitialisedString(size_t length, char **data);
nonnull vref VCreateSubstring(vref string, size_t offset, size_t length);
nonnull vref VCreateStringFormatted(const char *format, va_list ap);

/*
  Returns a null-terminated string. This function must only be used when the
  string was added through the string pool. (This should be true for all code
  running before the VM starts.)
*/
nonnull const char *VGetString(vref object);
nonnull char *VGetStringCopy(vref object);

/*
  Converts the object to a string and writes it to dst. The written string will
  be exactly as long as HeapStringLength returned. The string will not be zero
  terminated. A pointer just past the end of the written string is returned.
*/
nonnull char *VWriteString(vref value, char *dst);
nonnull char *VWriteSubstring(vref object, size_t offset, size_t length, char *dst);

nonnull vref VStringIndexOf(vref text, size_t startOffset, vref substring);


/*
  Creates a path object, if the supplied object isn't one already.
*/
nonnull vref VCreatePath(vref path);
nonnull const char *VGetPath(vref path, size_t *length);
nonnull bool VIsFile(vref object);
nonnull vref VPathFromParts(vref path, vref name, vref extension);


nonnull vref VCreateFilelist(vref value);
nonnull vref VCreateFilelistGlob(const char *pattern, size_t length);


nonnull vref *VCreateArray(size_t size);
nonnull vref VFinishArray(vref *array);
nonnull vref VCreateArrayFromData(const vref *values, size_t size);
nonnull vref VCreateArrayFromVector(const intvector *values);
nonnull vref VCreateArrayFromVectorSegment(const intvector *values, size_t start, size_t length);
nonnull vref VConcatList(vref list1, vref list2);
pureconst bool VIsCollectionType(VType type);
nonnull bool VIsCollection(vref object);
size_t VCollectionSize(vref value);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
nonnull bool VCollectionGet(vref object, vref indexObject, vref *value);


vref VEquals(vref value1, vref value2);
vref VLess(VM *vm, vref value1, vref value2);
vref VLessEquals(VM *vm, vref value1, vref value2);
vref VAdd(VM *vm, vref value1, vref value2);
vref VSub(VM *vm, vref value1, vref value2);
vref VMul(VM *vm, vref value1, vref value2);
vref VDiv(VM *vm, vref value1, vref value2);
vref VRem(VM *vm, vref value1, vref value2);
vref VAnd(VM *vm, vref value1, vref value2);
vref VNot(vref value);
vref VNeg(VM *vm, vref value);
vref VInv(VM *vm, vref value);
vref VValidIndex(VM *vm, vref collection, vref index);
vref VIndexedAccess(VM *vm, vref value1, vref value2);
vref VRange(VM *vm, vref value1, vref value2);
vref VConcat(VM *vm, vref value1, vref value2);
vref VConcatString(size_t count, vref *values);
