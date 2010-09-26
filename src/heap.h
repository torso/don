typedef enum
{
    TYPE_NULL_LITERAL,
    TYPE_BOOLEAN_LITERAL,
    TYPE_INTEGER_LITERAL,
    TYPE_STRING_LITERAL,
    TYPE_OBJECT
} ValueType;

typedef enum
{
    TYPE_BOOLEAN,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_EMPTY_LIST,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE
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
    uint emptyList;
} Heap;

typedef struct
{
    Heap *heap;
    IteratorType type;
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
} Iterator;

extern nonnull ErrorCode HeapInit(Heap *heap);
extern nonnull void HeapDispose(Heap *heap);

extern nonnull ObjectType HeapGetObjectType(Heap *heap, uint object);
extern nonnull size_t HeapGetObjectSize(Heap *heap, uint object);
extern nonnull const byte *HeapGetObjectData(Heap *heap, uint object);

extern nonnull byte *HeapAlloc(Heap *heap, ObjectType type, size_t size);
extern nonnull uint HeapFinishAlloc(Heap *heap, byte *objectData);

extern nonnull int HeapGetInteger(Heap *heap, uint object);

extern nonnull boolean HeapIsCollection(Heap *heap, uint object);
extern nonnull size_t HeapCollectionSize(Heap *heap, uint object);
extern nonnull void HeapCollectionIteratorInit(Heap *heap, Iterator *iter,
                                               uint object);
extern nonnull boolean HeapIteratorNext(Iterator *iter,
                                        ValueType *type, uint *value);
