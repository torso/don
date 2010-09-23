typedef enum
{
    TYPE_STRING
} ObjectType;

typedef struct
{
    byte *base;
    byte *free;
} Heap;

ErrorCode HeapInit(Heap *heap);
void HeapDispose(Heap *heap);

ObjectType HeapGetObjectType(Heap *heap, uint object);
size_t HeapGetObjectSize(Heap *heap, uint object);
const byte *HeapGetObjectData(Heap *heap, uint object);

byte *HeapAlloc(Heap *heap, ObjectType type, size_t size);
uint HeapFinishAlloc(Heap *heap, byte *objectData);
