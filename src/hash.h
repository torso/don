#define DIGEST_SIZE  64
#define BLOCK_SIZE  128

struct HashState
{
    uint64 pipe[16];
    byte buffer[BLOCK_SIZE];
    size_t processed;
    uint unprocessed;
};

extern nonnull void HashInit(HashState *state);
extern nonnull void HashUpdate(HashState *restrict state,
                               const byte *restrict data, size_t size);
extern nonnull void HashFinal(HashState *restrict state, byte *restrict hash);
extern nonnull void Hash(const byte *restrict data, size_t size,
                         byte *restrict hash);
