#include "blake2.h"

#define DIGEST_SIZE  64

struct HashState
{
    blake2b_state state;
};

extern nonnull void HashInit(HashState *state);
extern nonnull void HashUpdate(HashState *restrict state,
                               const byte *restrict data, size_t size);
extern nonnull void HashFinal(HashState *restrict state, byte *restrict hash);
extern nonnull void Hash(const byte *restrict data, size_t size,
                         byte *restrict hash);
