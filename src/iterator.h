typedef struct
{
    const RunState *state;
    uint index;
    uint length;
    uint bytecodeOffset;
    uint valueOffset;
} Iterator;

extern nonnull void IteratorInit(Iterator *restrict iterator,
                                 const RunState *restrict state,
                                 uint object);
extern nonnull boolean IteratorHasNext(Iterator *iterator);
extern nonnull void IteratorNext(Iterator *iterator);
extern nonnull void IteratorMove(Iterator *iterator, uint steps);
extern nonnull uint IteratorGetValueOffset(Iterator *iterator);
