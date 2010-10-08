typedef enum
{
    TYPE_BOOLEAN_TRUE,
    TYPE_BOOLEAN_FALSE,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_STRING_POOLED,
    TYPE_FILE,
    TYPE_EMPTY_LIST,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE,
    TYPE_ITERATOR
} ObjectType;

typedef enum
{
    ITER_EMPTY,
    ITER_OBJECT_ARRAY,
    ITER_INTEGER_RANGE
} IteratorType;

typedef struct
{
    byte *base;
    byte *free;
    uint booleanTrue;
    uint booleanFalse;
    uint emptyString;
    uint emptyList;
} Heap;

typedef struct IteratorState IteratorState;

struct IteratorState
{
    IteratorState *nextState;
    IteratorType type;
    boolean flatten;
    union
    {
        const uint *objectArray;
        int value;
    } current;
    union
    {
        const uint *objectArray;
        int value;
    } limit;
};

typedef struct
{
    Heap *heap;
    IteratorState state;
} Iterator;

extern nonnull ErrorCode HeapInit(Heap *heap);
extern nonnull void HeapDispose(Heap *heap);

extern nonnull ObjectType HeapGetObjectType(Heap *heap, uint object);
extern nonnull size_t HeapGetObjectSize(Heap *heap, uint object);
extern nonnull const byte *HeapGetObjectData(Heap *heap, uint object);

extern nonnull byte *HeapAlloc(Heap *heap, ObjectType type, size_t size);
extern nonnull uint HeapFinishAlloc(Heap *heap, byte *objectData);

extern nonnull uint HeapBoxInteger(Heap *heap, int value);
extern nonnull uint HeapBoxSize(Heap *heap, size_t value);
extern nonnull int HeapUnboxInteger(Heap *heap, uint value);

extern nonnull uint HeapCreateString(Heap *heap, const char *string,
                                     size_t length);
extern nonnull uint HeapCreatePooledString(Heap *heap, stringref string);
extern nonnull boolean HeapIsString(Heap *heap, uint object);
extern nonnull const char *HeapGetString(Heap *heap, uint object);
extern nonnull size_t HeapGetStringLength(Heap *heap, uint object);

extern nonnull uint HeapCreateFile(Heap *heap, fileref file);
extern nonnull fileref HeapGetFile(Heap *heap, uint object);

extern nonnull uint HeapCreateRange(Heap *heap,
                                    uint lowObject, uint highObject);

extern nonnull boolean HeapIsCollection(Heap *heap, uint object);
extern nonnull size_t HeapCollectionSize(Heap *heap, uint object);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull boolean HeapCollectionGet(Heap *heap, uint object,
                                         uint indexObject, uint *value);
extern nonnull void HeapCollectionIteratorInit(Heap *heap, Iterator *iter,
                                               uint object, boolean flatten);
extern nonnull boolean HeapIteratorNext(Iterator *iter, uint *value);

extern nonnull ErrorCode HeapCreateFilesetGlob(Heap *heap, const char *pattern,
                                               uint *value);
