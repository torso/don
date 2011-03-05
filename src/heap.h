typedef enum
{
    TYPE_BOOLEAN_TRUE,
    TYPE_BOOLEAN_FALSE,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_STRING_POOLED,
    TYPE_STRING_WRAPPED,
    TYPE_SUBSTRING,
    TYPE_FILE,
    TYPE_EMPTY_LIST,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE,
    TYPE_CONCAT_LIST,
    TYPE_ITERATOR,
    TYPE_FUTURE
} ObjectType;

typedef enum
{
    ITER_EMPTY,
    ITER_INDEXED,
    ITER_CONCAT_LIST
} IteratorType;

typedef struct
{
    /*
      Since an iterator can visit several collections, several iterators are
      needed. This field connects those states in proper order. For the main
      iterator, the meaning is special.

      For the main iterator, this will point to the currently active iterator.
      If null, the main iterator is the active iterator.

      For all other iterators, this will point to the immediate parent. If null,
      the main iterator is the parent. (Note that the main iterator might not
      have been allocated on the heap, and is thus not addressable through this
      field.)
    */
    objectref next;

    IteratorType type;
    objectref object;
    boolean flatten;
    union
    {
        size_t index;
    } current;
    union
    {
        size_t index;
    } limit;
} Iterator;

extern objectref HeapTrue;
extern objectref HeapFalse;
extern objectref HeapEmptyString;
extern objectref HeapEmptyList;
extern objectref HeapNewline;


extern void HeapInit(void);
extern void HeapDispose(void);

extern nonnull ObjectType HeapGetObjectType(objectref object);
extern nonnull size_t HeapGetObjectSize(objectref object);
extern nonnull const byte *HeapGetObjectData(objectref object);
extern nonnull void HeapHash(objectref object, HashState *hash);
extern nonnull boolean HeapEquals(objectref object1, objectref object2);
extern nonnull int HeapCompare(objectref object1, objectref object2);

extern nonnull byte *HeapAlloc(ObjectType type, size_t size);
extern nonnull objectref HeapFinishAlloc(byte *objectData);



extern nonnull boolean HeapIsTrue(objectref object);



extern nonnull objectref HeapBoxInteger(int value);
extern nonnull objectref HeapBoxUint(uint value);
extern nonnull objectref HeapBoxSize(size_t value);
extern nonnull int HeapUnboxInteger(objectref object);
extern nonnull size_t HeapUnboxSize(objectref object);
extern nonnull int HeapIntegerSign(objectref object);



extern nonnull objectref HeapCreateString(const char *string, size_t length);
extern nonnull objectref HeapCreateUninitialisedString(size_t length,
                                                       char **data);
extern nonnull objectref HeapCreatePooledString(stringref string);
extern nonnull objectref HeapCreateWrappedString(const char *string,
                                                 size_t length);
extern nonnull objectref HeapCreateSubstring(objectref string, size_t offset,
                                             size_t length);
extern nonnull boolean HeapIsString(objectref object);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern nonnull size_t HeapStringLength(objectref object);

/*
  Converts the object to a string and writes it to dst. The written string will
  be exactly as long as HeapStringLength returned. The string will not be zero
  terminated. A pointer just past the end of the written string is returned.
*/
extern nonnull char *HeapWriteString(objectref object, char *restrict dst);

extern nonnull char *HeapWriteSubstring(objectref object, size_t offset,
                                        size_t length, char *restrict dst);

extern nonnull objectref HeapStringIndexOf(objectref text, size_t startOffset,
                                           objectref substring);



extern nonnull objectref HeapCreateFile(fileref file);
extern nonnull boolean HeapIsFile(objectref object);
extern nonnull fileref HeapGetFile(objectref object);
extern nonnull fileref HeapGetAsFile(objectref object);
extern nonnull fileref HeapGetFileFromParts(objectref path, objectref name,
                                            objectref extension);



extern nonnull objectref HeapCreateRange(objectref lowObject,
                                         objectref highObject);
extern nonnull boolean HeapIsRange(objectref object);
extern nonnull objectref HeapRangeLow(objectref range);
extern nonnull objectref HeapRangeHigh(objectref range);



extern nonnull objectref HeapSplit(objectref string, objectref delimiter,
                                   boolean removeEmpty,
                                   boolean trimEmptyLastLine);

extern nonnull objectref HeapCreateArray(const objectref *values, size_t size);
extern nonnull objectref HeapCreateArrayFromVector(const intvector *values);
extern nonnull objectref HeapConcatList(objectref list1, objectref list2);
extern nonnull boolean HeapIsCollection(objectref object);
extern nonnull size_t HeapCollectionSize(objectref object);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull boolean HeapCollectionGet(objectref object,
                                         objectref indexObject,
                                         objectref *value);



extern nonnull objectref HeapCreateIterator(objectref object);
extern nonnull void HeapIteratorInit(Iterator *iter, objectref object,
                                     boolean flatten);
extern nonnull boolean HeapIteratorNext(Iterator *iter, objectref *value);

extern nonnull objectref HeapCreateFilesetGlob(const char *pattern);



extern objectref HeapCreateFutureValue(void);
extern void HeapSetFutureValue(objectref object, objectref value);
