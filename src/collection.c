#include <stdio.h>
#include "builder.h"
#include "bytevector.h"
#include "intvector.h"
#include "interpreterstate.h"
#include "value.h"
#include "collection.h"
#include "iterator.h"

boolean CollectionIsEmpty(const RunState *state, uint object)
{
    return CollectionGetSize(state, object) == 0;
}

uint CollectionGetSize(const RunState *state, uint object)
{
    ObjectType type;
    uint bytecodeOffset;

    type = ByteVectorRead(&state->heap, &object);
    assert(type == OBJECT_LIST);
    bytecodeOffset = ByteVectorGetPackUint(&state->heap, object);
    return ByteVectorGetPackUint(state->valueBytecode, bytecodeOffset);
}

uint CollectionGetElementValueOffset(const RunState *state, uint object,
                                     uint index)
{
    Iterator iterator;

    IteratorInit(&iterator, state, object);
    IteratorMove(&iterator, index + 1);
    return IteratorGetValueOffset(&iterator);
}
