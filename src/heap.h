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
    VM *vm;
    IteratorState state;
} Iterator;


extern nonnull ErrorCode HeapInit(VM *vm);
extern nonnull void HeapDispose(VM *vm);

extern nonnull ObjectType HeapGetObjectType(VM *vm, uint object);
extern nonnull size_t HeapGetObjectSize(VM *vm, uint object);
extern nonnull const byte *HeapGetObjectData(VM *vm, uint object);

extern nonnull byte *HeapAlloc(VM *vm, ObjectType type, size_t size);
extern nonnull uint HeapFinishAlloc(VM *vm, byte *objectData);

extern nonnull uint HeapBoxInteger(VM *vm, int value);
extern nonnull uint HeapBoxSize(VM *vm, size_t value);
extern nonnull int HeapUnboxInteger(VM *vm, uint value);

extern nonnull uint HeapCreateString(VM *vm, const char *string, size_t length);
extern nonnull uint HeapCreatePooledString(VM *vm, stringref string);
extern nonnull boolean HeapIsString(VM *vm, uint object);
extern nonnull const char *HeapGetString(VM *vm, uint object);
extern nonnull size_t HeapGetStringLength(VM *vm, uint object);

extern nonnull uint HeapCreateFile(VM *vm, fileref file);
extern nonnull fileref HeapGetFile(VM *vm, uint object);

extern nonnull uint HeapCreateRange(VM *vm, uint lowObject, uint highObject);

extern nonnull boolean HeapIsCollection(VM *vm, uint object);
extern nonnull size_t HeapCollectionSize(VM *vm, uint object);

/*
  Reads one value from the collection and returns it. The key is stored in
  [*type, *value], and the read value will overwrite them.

  Returns true if successful.
*/
extern nonnull boolean HeapCollectionGet(VM *vm, uint object, uint indexObject,
                                         uint *value);
extern nonnull void HeapCollectionIteratorInit(VM *vm, Iterator *iter,
                                               uint object, boolean flatten);
extern nonnull boolean HeapIteratorNext(Iterator *iter, uint *value);

extern nonnull uint HeapCreateFilesetGlob(VM *vm, const char *pattern);
