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
    TYPE_ITERATOR
} ObjectType;

typedef enum
{
    ITER_EMPTY,
    ITER_INDEXED,
    ITER_CONCAT_LIST
} IteratorType;

typedef struct IteratorState IteratorState;

struct IteratorState
{
    IteratorState *nextState;
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
};

typedef struct
{
    VM *vm;
    IteratorState state;
} Iterator;


extern nonnull ErrorCode HeapInit(VM *vm);
extern nonnull void HeapDispose(VM *vm);

extern nonnull ObjectType HeapGetObjectType(VM *vm, objectref object);
extern nonnull size_t HeapGetObjectSize(VM *vm, objectref object);
extern nonnull const byte *HeapGetObjectData(VM *vm, objectref object);
extern nonnull void HeapHash(VM *vm, objectref object, HashState *hash);
extern nonnull boolean HeapEquals(VM *vm, objectref object1, objectref object2);
extern nonnull int HeapCompare(VM *vm, objectref object1, objectref object2);

extern nonnull byte *HeapAlloc(VM *vm, ObjectType type, size_t size);
extern nonnull objectref HeapFinishAlloc(VM *vm, byte *objectData);



extern nonnull boolean HeapIsTrue(VM *vm, objectref object);



extern nonnull objectref HeapBoxInteger(VM *vm, int value);
extern nonnull objectref HeapBoxSize(VM *vm, size_t value);
extern nonnull int HeapUnboxInteger(VM *vm, objectref object);



extern nonnull objectref HeapCreateString(VM *vm, const char *string,
                                          size_t length);
extern nonnull objectref HeapCreatePooledString(VM *vm, stringref string);
extern nonnull objectref HeapCreateWrappedString(VM *vm, const char *string,
                                                 size_t length);
extern nonnull objectref HeapCreateSubstring(VM *vm, objectref string,
                                             size_t offset, size_t length);
extern nonnull boolean HeapIsString(VM *vm, objectref object);

/*
  Returns the size of the string in bytes. If the value isn't a string, the
  length of the value converted to a string (of the default form) is returned.
*/
extern nonnull size_t HeapStringLength(VM *vm, objectref object);

/*
  Converts the object to a string and writes it to dst. The written string will
  be exactly as long as HeapStringLength returned. The string will not be zero
  terminated. A pointer just past the end of the written string is returned.
*/
extern nonnull char *HeapWriteString(VM *restrict vm, objectref object,
                                     char *restrict dst);



extern nonnull objectref HeapCreateFile(VM *vm, fileref file);
extern nonnull fileref HeapGetFile(VM *vm, objectref object);



extern nonnull objectref HeapCreateRange(VM *vm, objectref lowObject,
                                         objectref highObject);
extern nonnull objectref HeapSplitLines(VM *vm, objectref string,
                                        boolean trimEmptyLastLine);

extern nonnull objectref HeapConcatList(VM *vm, objectref list1, objectref list2);
extern nonnull boolean HeapIsCollection(VM *vm, objectref object);
extern nonnull size_t HeapCollectionSize(VM *vm, objectref object);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull boolean HeapCollectionGet(VM *vm, objectref object,
                                         objectref indexObject,
                                         objectref *value);



extern nonnull objectref HeapCreateIterator(VM *vm, objectref object);
extern nonnull void HeapIteratorInit(VM *vm, Iterator *iter, objectref object,
                                     boolean flatten);
extern nonnull boolean HeapIteratorNext(Iterator *iter, objectref *value);

extern nonnull objectref HeapCreateFilesetGlob(VM *vm, const char *pattern);
