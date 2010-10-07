typedef enum
{
    TYPE_NULL_LITERAL,
    TYPE_BOOLEAN_LITERAL,
    TYPE_INTEGER_LITERAL,
    TYPE_STRING_LITERAL,
    TYPE_FILE_LITERAL,
    TYPE_OBJECT
} ValueType;

typedef enum
{
    TYPE_BOOLEAN,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_FILE,
    TYPE_EMPTY_LIST,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE,
    TYPE_FILESET,
    TYPE_ITERATOR
} ObjectType;

typedef enum
{
    ITER_EMPTY,
    ITER_OBJECT_ARRAY,
    ITER_INTEGER_RANGE,
    ITER_FILESET
} IteratorType;

typedef struct
{
    byte *base;
    byte *free;
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

extern nonnull uint HeapAllocString(Heap *heap, const char *string,
                                    size_t length);

extern nonnull boolean HeapIsCollection(Heap *heap, uint object);
extern nonnull size_t HeapCollectionSize(Heap *heap, uint object);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull boolean HeapCollectionGet(Heap *heap, uint object,
                                         ValueType *type, uint *value);
extern nonnull void HeapCollectionIteratorInit(Heap *heap, Iterator *iter,
                                               uint object, boolean flatten);
extern nonnull boolean HeapIteratorNext(Iterator *iter,
                                        ValueType *type, uint *value);

extern nonnull ErrorCode HeapCreateFilesetGlob(Heap *heap, const char *pattern,
                                               ValueType *restrict type,
                                               uint *restrict value);
