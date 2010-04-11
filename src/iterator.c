#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "interpreterstate.h"
#include "value.h"
#include "collection.h"
#include "iterator.h"

void IteratorInit(Iterator *iterator, const RunState *state, uint object)
{
    ObjectType type;

    iterator->state = state;
    iterator->index = 0;

    type = ByteVectorRead(&state->heap, &object);
    assert(type == OBJECT_LIST);
    iterator->bytecodeOffset = ByteVectorReadPackUint(&state->heap, &object);
    iterator->valueOffset = ByteVectorReadPackUint(&state->heap, &object);
    iterator->length = ByteVectorReadPackUint(state->valueBytecode,
                                              &iterator->bytecodeOffset);
}

boolean IteratorHasNext(Iterator *iterator)
{
    return iterator->index + 1 < iterator->length;
}

void IteratorNext(Iterator *iterator)
{
    assert(iterator->index < iterator->length);
    iterator->index++;
    ByteVectorSkipPackUint(iterator->state->valueBytecode,
                           &iterator->bytecodeOffset);
}

void IteratorMove(Iterator *iterator, uint steps)
{
    while (steps--)
    {
        IteratorNext(iterator);
    }
}

uint IteratorGetValueOffset(Iterator *iterator)
{
    return ValueGetRelativeOffset(iterator->state,
                                  iterator->valueOffset,
                                  iterator->bytecodeOffset);
}
