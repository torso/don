/*
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along with
   this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#include "config.h"
#include <string.h>
#include "common.h"
#include "blake2.h"
#include "blake2-config.h"

#include <emmintrin.h>
#if defined(HAVE_SSSE3)
#include <tmmintrin.h>
#endif
#if defined(HAVE_SSE41)
#include <smmintrin.h>
#endif
#if defined(HAVE_AVX)
#include <immintrin.h>
#endif
#if defined(HAVE_XOP)
#include <x86intrin.h>
#endif

#include "blake2b-round.h"

ALIGN( 64 ) static const uint64_t blake2b_IV[8] =
{
    ((uint64_t)0x6a09e667 << 32) + 0xf3bcc908,
    ((uint64_t)0xbb67ae85 << 32) + 0x84caa73b,
    ((uint64_t)0x3c6ef372 << 32) + 0xfe94f82b,
    ((uint64_t)0xa54ff53a << 32) + 0x5f1d36f1,
    ((uint64_t)0x510e527f << 32) + 0xade682d1,
    ((uint64_t)0x9b05688c << 32) + 0x2b3e6c1f,
    ((uint64_t)0x1f83d9ab << 32) + 0xfb41bd6b,
    ((uint64_t)0x5be0cd19 << 32) + 0x137e2179
};


static int blake2b_set_lastblock( blake2b_state *S )
{
  S->f[0] = ~0UL;
  return 0;
}


static int blake2b_increment_counter( blake2b_state *S, const uint64_t inc )
{
#if __x86_64__
  /* ADD/ADC chain */
  __uint128_t t = ( ( __uint128_t )S->t[1] << 64 ) | S->t[0];
  t += inc;
  S->t[0] = ( uint64_t )( t >>  0 );
  S->t[1] = ( uint64_t )( t >> 64 );
#else
  S->t[0] += inc;
  S->t[1] += ( S->t[0] < inc );
#endif
  return 0;
}


void blake2b_init( blake2b_state *S )
{
  int i;
  memset( S, 0, sizeof( blake2b_state ) );
  for( i = 0; i < 8; ++i ) S->h[i] = blake2b_IV[i];
}

static int blake2b_compress( blake2b_state *S, const uint8_t block[BLAKE2B_BLOCKBYTES] )
{
  __m128i row1l, row1h;
  __m128i row2l, row2h;
  __m128i row3l, row3h;
  __m128i row4l, row4h;
  __m128i b0, b1;
  __m128i t0, t1;
#if defined(HAVE_SSSE3) && !defined(HAVE_XOP)
  const __m128i r16 = _mm_setr_epi8( 2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9 );
  const __m128i r24 = _mm_setr_epi8( 3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10 );
#endif
#if defined(HAVE_SSE41)
  const __m128i m0 = LOADU( block + 00 );
  const __m128i m1 = LOADU( block + 16 );
  const __m128i m2 = LOADU( block + 32 );
  const __m128i m3 = LOADU( block + 48 );
  const __m128i m4 = LOADU( block + 64 );
  const __m128i m5 = LOADU( block + 80 );
  const __m128i m6 = LOADU( block + 96 );
  const __m128i m7 = LOADU( block + 112 );
#else
  const uint64_t  m0 = ( ( uint64_t * )block )[ 0];
  const uint64_t  m1 = ( ( uint64_t * )block )[ 1];
  const uint64_t  m2 = ( ( uint64_t * )block )[ 2];
  const uint64_t  m3 = ( ( uint64_t * )block )[ 3];
  const uint64_t  m4 = ( ( uint64_t * )block )[ 4];
  const uint64_t  m5 = ( ( uint64_t * )block )[ 5];
  const uint64_t  m6 = ( ( uint64_t * )block )[ 6];
  const uint64_t  m7 = ( ( uint64_t * )block )[ 7];
  const uint64_t  m8 = ( ( uint64_t * )block )[ 8];
  const uint64_t  m9 = ( ( uint64_t * )block )[ 9];
  const uint64_t m10 = ( ( uint64_t * )block )[10];
  const uint64_t m11 = ( ( uint64_t * )block )[11];
  const uint64_t m12 = ( ( uint64_t * )block )[12];
  const uint64_t m13 = ( ( uint64_t * )block )[13];
  const uint64_t m14 = ( ( uint64_t * )block )[14];
  const uint64_t m15 = ( ( uint64_t * )block )[15];
#endif
  row1l = LOAD( &S->h[0] );
  row1h = LOAD( &S->h[2] );
  row2l = LOAD( &S->h[4] );
  row2h = LOAD( &S->h[6] );
  row3l = LOAD( &blake2b_IV[0] );
  row3h = LOAD( &blake2b_IV[2] );
  row4l = _mm_xor_si128( LOAD( &blake2b_IV[4] ), LOAD( &S->t[0] ) );
  row4h = _mm_xor_si128( LOAD( &blake2b_IV[6] ), LOAD( &S->f[0] ) );
  ROUND( 0 );
  ROUND( 1 );
  ROUND( 2 );
  ROUND( 3 );
  ROUND( 4 );
  ROUND( 5 );
  ROUND( 6 );
  ROUND( 7 );
  ROUND( 8 );
  ROUND( 9 );
  ROUND( 10 );
  ROUND( 11 );
  row1l = _mm_xor_si128( row3l, row1l );
  row1h = _mm_xor_si128( row3h, row1h );
  STORE( &S->h[0], _mm_xor_si128( LOAD( &S->h[0] ), row1l ) );
  STORE( &S->h[2], _mm_xor_si128( LOAD( &S->h[2] ), row1h ) );
  row2l = _mm_xor_si128( row4l, row2l );
  row2h = _mm_xor_si128( row4h, row2h );
  STORE( &S->h[4], _mm_xor_si128( LOAD( &S->h[4] ), row2l ) );
  STORE( &S->h[6], _mm_xor_si128( LOAD( &S->h[6] ), row2h ) );
  return 0;
}


int blake2b_update( blake2b_state *S, const uint8_t *in, size_t inlen )
{
  while( inlen > 0 )
  {
    size_t left = S->buflen;
    size_t fill = 2 * BLAKE2B_BLOCKBYTES - left;

    if( inlen > fill )
    {
      memcpy( S->buf + left, in, fill ); /* Fill buffer */
      S->buflen += fill;
      blake2b_increment_counter( S, BLAKE2B_BLOCKBYTES );
      blake2b_compress( S, S->buf ); /* Compress */
      memcpy( S->buf, S->buf + BLAKE2B_BLOCKBYTES, BLAKE2B_BLOCKBYTES ); /* Shift buffer left */
      S->buflen -= BLAKE2B_BLOCKBYTES;
      in += fill;
      inlen -= fill;
    }
    else /* inlen <= fill */
    {
      memcpy( S->buf + left, in, inlen );
      S->buflen += inlen; /* Be lazy, do not compress */
      in += inlen;
      inlen -= inlen;
    }
  }

  return 0;
}


int blake2b_final( blake2b_state *S, uint8_t *out, uint8_t outlen )
{
  if( S->buflen > BLAKE2B_BLOCKBYTES )
  {
    blake2b_increment_counter( S, BLAKE2B_BLOCKBYTES );
    blake2b_compress( S, S->buf );
    S->buflen -= BLAKE2B_BLOCKBYTES;
    memcpy( S->buf, S->buf + BLAKE2B_BLOCKBYTES, S->buflen );
  }

  blake2b_increment_counter( S, S->buflen );
  blake2b_set_lastblock( S );
  memset( S->buf + S->buflen, 0, 2 * BLAKE2B_BLOCKBYTES - S->buflen ); /* Padding */
  blake2b_compress( S, S->buf );
  memcpy( out, &S->h[0], outlen );
  return 0;
}
