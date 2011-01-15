#include <string.h>
#include "common.h"
#include "hash.h"

#define SKEIN_VERSION (1)
#define SKEIN_ID_STRING_LE (0x33414853) /* "SHA3" (little-endian)*/

#define SKEIN_MODIFIER_WORDS (2)
#define STATE_WORDS (8)
#define STATE_BYTES (8*STATE_WORDS)
#define STATE_BITS (64*STATE_WORDS)
#define BLOCK_BYTES (8*STATE_WORDS)

#define FLAG_FIRST ((uint64)1 << 62)
#define FLAG_FINAL ((uint64)1 << 63)

#define BT_CFG ((uint64)4 << 56)
#define BT_MSG ((uint64)48 << 56)
#define BT_OUT ((uint64)63 << 56)

#define SKEIN_SCHEMA_VER (((uint64)SKEIN_VERSION << 32) + SKEIN_ID_STRING_LE)
#define SKEIN_CFG_STR_LEN (4*8)

enum
{
    R_512_0_0=46, R_512_0_1=36, R_512_0_2=19, R_512_0_3=37,
    R_512_1_0=33, R_512_1_1=27, R_512_1_2=14, R_512_1_3=42,
    R_512_2_0=17, R_512_2_1=49, R_512_2_2=36, R_512_2_3=39,
    R_512_3_0=44, R_512_3_1= 9, R_512_3_2=54, R_512_3_3=56,
    R_512_4_0=39, R_512_4_1=30, R_512_4_2=34, R_512_4_3=24,
    R_512_5_0=13, R_512_5_1=50, R_512_5_2=10, R_512_5_3=17,
    R_512_6_0=25, R_512_6_1=29, R_512_6_2=39, R_512_6_3=43,
    R_512_7_0= 8, R_512_7_1=35, R_512_7_2=56, R_512_7_3=22
};

#ifdef LITTLE_ENDIAN
#define get64 memcpy
#define put64 memcpy
#else
static void get64(uint64 *restrict dst, const byte *restrict src, size_t size)
{
    size_t n;

    for (n = 0; n < size; n += 8)
    {
        dst[n / 8] = (uint64)src[n] +
            ((uint64)src[n + 1] << 8) +
            ((uint64)src[n + 2] << 16) +
            ((uint64)src[n + 3] << 24) +
            ((uint64)src[n + 4] << 32) +
            ((uint64)src[n + 5] << 40) +
            ((uint64)src[n + 6] << 48) +
            ((uint64)src[n + 7] << 56);
    }
}

static void put64(byte *restrict dst, const uint64 *restrict src, size_t size)
{
    size_t n;

    for (n=0; n < size; n++)
    {
        dst[n] = (byte)(src[n >> 3] >> (8 * (n & 7)));
    }
}
#endif

static uint64 rotl64(uint64 x, uint n)
{
    return (x << n) | (x >> (64 - n));
}

static void startType(HashState *state, uint64 blockType)
{
    state->t[0] = 0;
    state->t[1] = FLAG_FIRST | blockType;
    state->bufferSize = 0;
}

#define INJECT_KEY(r)                                   \
    for (i = 0; i < STATE_WORDS; i++)                   \
    {                                                   \
        x[i] += ks[((r)+i) % (STATE_WORDS+1)];          \
    }                                                   \
    x[STATE_WORDS-3] += ts[(r) % 3];                    \
    x[STATE_WORDS-2] += ts[((r)+1) % 3];                \
    x[STATE_WORDS-1] += (r); /* avoid slide attacks */

static void processBlock(HashState *state, const byte *blkPtr, size_t blkCnt, size_t byteCntAdd)
{
    size_t i, r;
    uint64 ts[3]; /* key schedule: tweak */
    uint64 ks[STATE_WORDS+1]; /* key schedule: chaining vars */
    uint64 x[STATE_WORDS]; /* local copy of vars */
    uint64 w[STATE_WORDS]; /* local copy of input block */

    assert(blkCnt);
    do
    {
        /* This implementation only supports 2**64 input bytes (no carry out
           here). */
        state->t[0] += byteCntAdd; /* update processed length */

        /* precompute the key schedule for this block */
        ks[STATE_WORDS] = ((uint64)0x1bd11bda << 32) + 0xa9fc1a22;
        for (i = 0; i < STATE_WORDS; i++)
        {
            ks[i] = state->x[i];
            ks[STATE_WORDS] ^= state->x[i];
        }
        ts[0] = state->t[0];
        ts[1] = state->t[1];
        ts[2] = ts[0] ^ ts[1];

        get64(w, blkPtr, STATE_BYTES);

        /* Do the first full key injection. */
        for (i = 0; i < STATE_WORDS; i++)
        {
            x[i] = w[i] + ks[i];
        }
        x[STATE_WORDS-3] += ts[0];
        x[STATE_WORDS-2] += ts[1];

        for (r = 1; r <= 72 / 8; r++)
        {
            x[0] += x[1]; x[1] = rotl64(x[1], R_512_0_0); x[1] ^= x[0];
            x[2] += x[3]; x[3] = rotl64(x[3], R_512_0_1); x[3] ^= x[2];
            x[4] += x[5]; x[5] = rotl64(x[5], R_512_0_2); x[5] ^= x[4];
            x[6] += x[7]; x[7] = rotl64(x[7], R_512_0_3); x[7] ^= x[6];

            x[2] += x[1]; x[1] = rotl64(x[1], R_512_1_0); x[1] ^= x[2];
            x[4] += x[7]; x[7] = rotl64(x[7], R_512_1_1); x[7] ^= x[4];
            x[6] += x[5]; x[5] = rotl64(x[5], R_512_1_2); x[5] ^= x[6];
            x[0] += x[3]; x[3] = rotl64(x[3], R_512_1_3); x[3] ^= x[0];

            x[4] += x[1]; x[1] = rotl64(x[1], R_512_2_0); x[1] ^= x[4];
            x[6] += x[3]; x[3] = rotl64(x[3], R_512_2_1); x[3] ^= x[6];
            x[0] += x[5]; x[5] = rotl64(x[5], R_512_2_2); x[5] ^= x[0];
            x[2] += x[7]; x[7] = rotl64(x[7], R_512_2_3); x[7] ^= x[2];

            x[6] += x[1]; x[1] = rotl64(x[1], R_512_3_0); x[1] ^= x[6];
            x[0] += x[7]; x[7] = rotl64(x[7], R_512_3_1); x[7] ^= x[0];
            x[2] += x[5]; x[5] = rotl64(x[5], R_512_3_2); x[5] ^= x[2];
            x[4] += x[3]; x[3] = rotl64(x[3], R_512_3_3); x[3] ^= x[4];
            INJECT_KEY(2*r-1);

            x[0] += x[1]; x[1] = rotl64(x[1], R_512_4_0); x[1] ^= x[0];
            x[2] += x[3]; x[3] = rotl64(x[3], R_512_4_1); x[3] ^= x[2];
            x[4] += x[5]; x[5] = rotl64(x[5], R_512_4_2); x[5] ^= x[4];
            x[6] += x[7]; x[7] = rotl64(x[7], R_512_4_3); x[7] ^= x[6];

            x[2] += x[1]; x[1] = rotl64(x[1], R_512_5_0); x[1] ^= x[2];
            x[4] += x[7]; x[7] = rotl64(x[7], R_512_5_1); x[7] ^= x[4];
            x[6] += x[5]; x[5] = rotl64(x[5], R_512_5_2); x[5] ^= x[6];
            x[0] += x[3]; x[3] = rotl64(x[3], R_512_5_3); x[3] ^= x[0];

            x[4] += x[1]; x[1] = rotl64(x[1], R_512_6_0); x[1] ^= x[4];
            x[6] += x[3]; x[3] = rotl64(x[3], R_512_6_1); x[3] ^= x[6];
            x[0] += x[5]; x[5] = rotl64(x[5], R_512_6_2); x[5] ^= x[0];
            x[2] += x[7]; x[7] = rotl64(x[7], R_512_6_3); x[7] ^= x[2];

            x[6] += x[1]; x[1] = rotl64(x[1], R_512_7_0); x[1] ^= x[6];
            x[0] += x[7]; x[7] = rotl64(x[7], R_512_7_1); x[7] ^= x[0];
            x[2] += x[5]; x[5] = rotl64(x[5], R_512_7_2); x[5] ^= x[2];
            x[4] += x[3]; x[3] = rotl64(x[3], R_512_7_3); x[3] ^= x[4];
            INJECT_KEY(2*r);
        }
        /* do the final "feedforward" xor, update context chaining vars */
        for (i = 0; i < STATE_WORDS; i++)
        {
            state->x[i] = x[i] ^ w[i];
        }

        state->t[1] &= ~FLAG_FIRST;
        blkPtr += BLOCK_BYTES;
    }
    while (--blkCnt);
}

void HashInit(HashState *state)
{
    union
    {
        byte b[STATE_BYTES];
        uint64 w[STATE_WORDS];
    } cfg;

    startType(state, BT_CFG | FLAG_FINAL);

    memset(&cfg.w, 0, sizeof(cfg.w));
    cfg.w[0] = SKEIN_SCHEMA_VER;
    cfg.w[1] = 512; /* hash result length in bits */

    memset(state->x, 0, sizeof(state->x));
    processBlock(state, cfg.b, 1, SKEIN_CFG_STR_LEN);

    startType(state, BT_MSG);
}

void HashUpdate(HashState *restrict state,
                const byte *restrict data, size_t size)
{
    size_t n;

    assert(state->bufferSize <= BLOCK_BYTES);

    if (state->bufferSize + size > BLOCK_BYTES)
    {
        if (state->bufferSize)
        {
            n = BLOCK_BYTES - state->bufferSize;
            memcpy(&state->buffer[state->bufferSize], data, n);
            size -= n;
            data += n;
            processBlock(state, state->buffer, 1, BLOCK_BYTES);
            state->bufferSize = 0;
        }
        if (size > BLOCK_BYTES)
        {
            n = size / BLOCK_BYTES;
            processBlock(state, data, n, BLOCK_BYTES);
            size -= n * BLOCK_BYTES;
            data += n * BLOCK_BYTES;
        }
        assert(state->bufferSize == 0);
    }

    if (size)
    {
        assert(size + state->bufferSize <= BLOCK_BYTES);
        memcpy(&state->buffer[state->bufferSize], data, size);
        state->bufferSize += size;
    }
}

void HashFinal(HashState *restrict state, byte *restrict hash)
{
    assert(state->bufferSize <= BLOCK_BYTES);

    state->t[1] |= FLAG_FINAL;
    memset(&state->buffer[state->bufferSize], 0, BLOCK_BYTES - state->bufferSize);
    processBlock(state, state->buffer, 1, state->bufferSize);

    memset(state->buffer, 0, BLOCK_BYTES);
    startType(state, BT_OUT | FLAG_FINAL);
    processBlock(state, state->buffer, 1, sizeof(uint64));
    put64(hash, state->x, BLOCK_BYTES);
}

void Hash(const byte *restrict data, size_t size, byte *restrict hash)
{
    HashState state;

    HashInit(&state);
    HashUpdate(&state, data, size);
    HashFinal(&state, hash);
}
