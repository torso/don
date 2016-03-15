#include "config.h"
#include "common.h"
#include "hash.h"


void HashInit(HashState *state)
{
    blake2b_init(&state->state);
}

void HashUpdate(HashState *restrict state,
                const byte *restrict data, size_t size)
{
    blake2b_update(&state->state, data, size);
}

void HashFinal(HashState *restrict state, byte *restrict hash)
{
    blake2b_final(&state->state, hash, DIGEST_SIZE);
}

void Hash(const byte *restrict data, size_t size, byte *restrict hash)
{
    HashState state;

    HashInit(&state);
    HashUpdate(&state, data, size);
    HashFinal(&state, hash);
}
