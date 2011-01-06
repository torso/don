#define DIGEST_SIZE  64

struct HashState
{
    size_t bufferSize;
    byte buffer[64];
    uint64 t[2];
    uint64 x[8];
};

extern nonnull void HashInit(HashState *state);
extern nonnull void HashUpdate(HashState *restrict state,
                               const byte *restrict data, size_t size);
extern nonnull void HashFinal(HashState *restrict state, byte *restrict hash);
extern nonnull void Hash(const byte *restrict data, size_t size,
                         byte *restrict hash);
