/* Hash algorithm: Blue Midnight Wish */

#include <string.h>
#include "common.h"
#include "hash.h"

#define rotl64(x, n) (((x) << n) | ((x) >> (64 - n)))
#define rotr64(x, n) (((x) >> n) | ((x) << (64 - n)))

#define shl(x, n) ((x) << n)
#define shr(x, n) ((x) >> n)

const uint64 initialPipe[16] =
{
    0x8081828384858687, 0x88898a8b8c8d8e8f,
    0x9091929394959697, 0x98999a9b9c9d9e9f,
    0xa0a1a2a3a4a5a6a7, 0xa8a9aaabacadaeaf,
    0xb0b1b2b3b4b5b6b7, 0xb8b9babbbcbdbebf,
    0xc0c1c2c3c4c5c6c7, 0xc8c9cacbcccdcecf,
    0xd0d1d2d3d4d5d6d7, 0xd8d9dadbdcdddedf,
    0xe0e1e2e3e4e5e6e7, 0xe8e9eaebecedeeef,
    0xf0f1f2f3f4f5f6f7, 0xf8f9fafbfcfdfeff
};

#define s64_0(x) (shr((x), 1) ^ shl((x), 3) ^ rotl64((x), 4) ^ rotl64((x), 37))
#define s64_1(x) (shr((x), 1) ^ shl((x), 2) ^ rotl64((x), 13) ^ rotl64((x), 43))
#define s64_2(x) (shr((x), 2) ^ shl((x), 1) ^ rotl64((x), 19) ^ rotl64((x), 53))
#define s64_3(x) (shr((x), 2) ^ shl((x), 2) ^ rotl64((x), 28) ^ rotl64((x), 59))
#define s64_4(x) (shr((x), 1) ^ (x))
#define s64_5(x) (shr((x), 2) ^ (x))
#define r64_1(x) rotl64((x), 5)
#define r64_2(x) rotl64((x), 11)
#define r64_3(x) rotl64((x), 27)
#define r64_4(x) rotl64((x), 32)
#define r64_5(x) rotl64((x), 37)
#define r64_6(x) rotl64((x), 43)
#define r64_7(x) rotl64((x), 53)

static void compression512(const uint64 *restrict data64, uint64 *restrict p512)
{
    uint64 XL64, XH64, TempEven64, TempOdd64;
    uint64 p512_00, p512_01, p512_02, p512_03, p512_04, p512_05, p512_06, p512_07;
    uint64 p512_08, p512_09, p512_10, p512_11, p512_12, p512_13, p512_14, p512_15;
    uint64 p512_16, p512_17, p512_18, p512_19, p512_20, p512_21, p512_22, p512_23;
    uint64 p512_24, p512_25, p512_26, p512_27, p512_28, p512_29, p512_30, p512_31;
    uint64 t512_16, t512_17, t512_18, t512_19, t512_20, t512_21;
    uint64 td64_00, td64_01, td64_02, td64_03, td64_04, td64_05, td64_06, td64_07;
    uint64 td64_08, td64_09, td64_10, td64_11, td64_12, td64_13, td64_14, td64_15;

    /* Mix the message block with the previous pipe. */
    p512_00 = p512[0] ^ data64[0];
    p512_01 = p512[1] ^ data64[1];
    p512_02 = p512[2] ^ data64[2];
    p512_03 = p512[3] ^ data64[3];
    p512_04 = p512[4] ^ data64[4];
    p512_05 = p512[5] ^ data64[5];
    p512_06 = p512[6] ^ data64[6];
    p512_07 = p512[7] ^ data64[7];
    p512_08 = p512[8] ^ data64[8];
    p512_09 = p512[9] ^ data64[9];
    p512_10 = p512[10] ^ data64[10];
    p512_11 = p512[11] ^ data64[11];
    p512_12 = p512[12] ^ data64[12];
    p512_13 = p512[13] ^ data64[13];
    p512_14 = p512[14] ^ data64[14];
    p512_15 = p512[15] ^ data64[15];

    /* This is the tweak */
    td64_00 = rotl64(data64[0], 1);
    td64_01 = rotl64(data64[1], 2);
    td64_02 = rotl64(data64[2], 3);
    td64_03 = rotl64(data64[3], 4);
    td64_04 = rotl64(data64[4], 5);
    td64_05 = rotl64(data64[5], 6);
    td64_06 = rotl64(data64[6], 7);
    td64_07 = rotl64(data64[7], 8);
    td64_08 = rotl64(data64[8], 9);
    td64_09 = rotl64(data64[9], 10);
    td64_10 = rotl64(data64[10], 11);
    td64_11 = rotl64(data64[11], 12);
    td64_12 = rotl64(data64[12], 13);
    td64_13 = rotl64(data64[13], 14);
    td64_14 = rotl64(data64[14], 15);
    td64_15 = rotl64(data64[15], 16);

    t512_16 = p512_01 - p512_14;
    t512_17 = p512_15 - p512_12;
    t512_18 = p512_14 - p512_07;
    t512_19 = p512_13 - p512_06;
    t512_20 = p512_08 - p512_05;
    t512_21 = p512_08 - p512_01;

    p512_16 = (p512_05 + t512_18 + p512_10 + p512_13);
    p512_16 = s64_0(p512_16) + p512[1];
    p512_17 = (p512_06 - p512_08 + p512_11 + p512_14 - p512_15);
    p512_17 = s64_1(p512_17) + p512[2];
    p512_18 = (p512_00 + p512_07 + p512_09 + t512_17);
    p512_18 = s64_2(p512_18) + p512[3];
    p512_19 = (p512_00 + t512_21 - p512_10 + p512_13);
    p512_19 = s64_3(p512_19) + p512[4];
    p512_20 = (t512_16 + p512_02 + p512_09 - p512_11);
    p512_20 = s64_4(p512_20) + p512[5];
    p512_21 = (p512_03 - p512_02 + p512_10 + t512_17);
    p512_21 = s64_0(p512_21) + p512[6];
    p512_22 = (p512_04 - p512_00 - p512_03 - p512_11 + p512_13);
    p512_22 = s64_1(p512_22) + p512[7];
    p512_23 = (t512_16 - p512_04 - p512_05 - p512_12);
    p512_23 = s64_2(p512_23) + p512[8];
    p512_24 = (p512_02 - p512_05 + t512_19 - p512_15);
    p512_24 = s64_3(p512_24) + p512[9];
    p512_25 = (p512_00 - p512_03 + p512_06 + t512_18);
    p512_25 = s64_4(p512_25) + p512[10];
    p512_26 = (t512_21 - p512_04 - p512_07 + p512_15);
    p512_26 = s64_0(p512_26) + p512[11];
    p512_27 = (t512_20 - p512_00 - p512_02 + p512_09);
    p512_27 = s64_1(p512_27) + p512[12];
    p512_28 = (p512_01 + p512_03 - p512_06 - p512_09 + p512_10);
    p512_28 = s64_2(p512_28) + p512[13];
    p512_29 = (p512_02 + p512_04 + p512_07 + p512_10 + p512_11);
    p512_29 = s64_3(p512_29) + p512[14];
    p512_30 = (p512_03 + t512_20 - p512_11 - p512_12);
    p512_14 = s64_4(p512_30) + p512[15];
    p512_31 = (p512_12 - p512_04 + t512_19 - p512_09);
    p512_15 = s64_0(p512_31) + p512[0];

    p512_00 = p512_16;
    p512_01 = p512_17;
    p512_02 = p512_18;
    p512_03 = p512_19;
    p512_04 = p512_20;
    p512_05 = p512_21;
    p512_06 = p512_22;
    p512_07 = p512_23;
    p512_08 = p512_24;
    p512_09 = p512_25;
    p512_10 = p512_26;
    p512_11 = p512_27;
    p512_12 = p512_28;
    p512_13 = p512_29;

    /* This is the Message expansion. */
    /* It has 16 rounds. */
    p512_16 = s64_1(p512_00) + s64_2(p512_01) + s64_3(p512_02) + s64_0(p512_03)
        + s64_1(p512_04) + s64_2(p512_05) + s64_3(p512_06) + s64_0(p512_07)
        + s64_1(p512_08) + s64_2(p512_09) + s64_3(p512_10) + s64_0(p512_11)
        + s64_1(p512_12) + s64_2(p512_13) + s64_3(p512_14) + s64_0(p512_15)
        + ((td64_00 + td64_03 - td64_10 + 0x5555555555555550) ^ p512[7]);
    XL64 = p512_16;
    p512_17 = s64_1(p512_01) + s64_2(p512_02) + s64_3(p512_03) + s64_0(p512_04)
        + s64_1(p512_05) + s64_2(p512_06) + s64_3(p512_07) + s64_0(p512_08)
        + s64_1(p512_09) + s64_2(p512_10) + s64_3(p512_11) + s64_0(p512_12)
        + s64_1(p512_13) + s64_2(p512_14) + s64_3(p512_15) + s64_0(p512_16)
        + ((td64_01 + td64_04 - td64_11 + 0x5aaaaaaaaaaaaaa5) ^ p512[8]);
    XL64 ^= p512_17;
    TempEven64 = p512_14 + p512_12 + p512_10 + p512_08 + p512_06 + p512_04 + p512_02;
    TempOdd64 = p512_15 + p512_13 + p512_11 + p512_09 + p512_07 + p512_05 + p512_03;

    /* expand64_22(18); */
    p512_18 = TempEven64 + r64_1(p512_03) + r64_2(p512_05)
        + r64_3(p512_07) + r64_4(p512_09)
        + r64_5(p512_11) + r64_6(p512_13)
        + r64_7(p512_15) + s64_4(p512_16) + s64_5(p512_17)
        + ((td64_02 + td64_05 - td64_12 + 0x5ffffffffffffffa) ^ p512[9]);
    XL64 ^= p512_18;
    /* expand64_21(19); */
    p512_19 = TempOdd64 + r64_1(p512_04) + r64_2(p512_06)
        + r64_3(p512_08) + r64_4(p512_10)
        + r64_5(p512_12) + r64_6(p512_14)
        + r64_7(p512_16) + s64_4(p512_17) + s64_5(p512_18)
        + ((td64_03 + td64_06 - td64_13 + 0x655555555555554f) ^ p512[10]);
    XL64 ^= p512_19;
    TempEven64 = TempEven64 + p512_16 - p512_02;
    /* expand64_22(20); */
    p512_20 = TempEven64 + r64_1(p512_05) + r64_2(p512_07)
        + r64_3(p512_09) + r64_4(p512_11)
        + r64_5(p512_13) + r64_6(p512_15)
        + r64_7(p512_17) + s64_4(p512_18) + s64_5(p512_19)
        + ((td64_04 + td64_07 - td64_14 + 0x6aaaaaaaaaaaaaa4) ^ p512[11]);
    XL64 ^= p512_20;
    TempOdd64 = TempOdd64 + p512_17 - p512_03;
    /* expand64_21(21); */
    p512_21 = TempOdd64 + r64_1(p512_06) + r64_2(p512_08)
        + r64_3(p512_10) + r64_4(p512_12)
        + r64_5(p512_14) + r64_6(p512_16)
        + r64_7(p512_18) + s64_4(p512_19) + s64_5(p512_20)
        + ((td64_05 + td64_08 - td64_15 + 0x6ffffffffffffff9) ^ p512[12]);
    XL64 ^= p512_21;
    TempEven64 += p512_18; TempEven64 -= p512_04;
    /* expand64_22(22); */
    p512_22 = TempEven64 + r64_1(p512_07) + r64_2(p512_09)
        + r64_3(p512_11) + r64_4(p512_13)
        + r64_5(p512_15) + r64_6(p512_17)
        + r64_7(p512_19) + s64_4(p512_20) + s64_5(p512_21)
        + ((td64_06 + td64_09 - td64_00 + 0x755555555555554e) ^ p512[13]);
    XL64 ^= p512_22;
    TempOdd64 += p512_19; TempOdd64 -= p512_05;
    /* expand64_21(23); */
    p512_23 = TempOdd64 + r64_1(p512_08) + r64_2(p512_10)
        + r64_3(p512_12) + r64_4(p512_14)
        + r64_5(p512_16) + r64_6(p512_18)
        + r64_7(p512_20) + s64_4(p512_21) + s64_5(p512_22)
        + ((td64_07 + td64_10 - td64_01 + 0x7aaaaaaaaaaaaaa3) ^ p512[14]);
    XL64 ^= p512_23;
    TempEven64 += p512_20; TempEven64 -= p512_06;
    /* expand64_22(24); */
    p512_24 = TempEven64 + r64_1(p512_09) + r64_2(p512_11)
        + r64_3(p512_13) + r64_4(p512_15)
        + r64_5(p512_17) + r64_6(p512_19)
        + r64_7(p512_21) + s64_4(p512_22) + s64_5(p512_23)
        + ((td64_08 + td64_11 - td64_02 + 0x7ffffffffffffff8) ^ p512[15]);
    XH64 = XL64^p512_24;
    TempOdd64 += p512_21; TempOdd64 -= p512_07;
    /* expand64_21(25); */
    p512_25 = TempOdd64 + r64_1(p512_10) + r64_2(p512_12)
        + r64_3(p512_14) + r64_4(p512_16)
        + r64_5(p512_18) + r64_6(p512_20)
        + r64_7(p512_22) + s64_4(p512_23) + s64_5(p512_24)
        + ((td64_09 + td64_12 - td64_03 + 0x855555555555554d) ^ p512[0]);
    XH64 ^= p512_25;
    TempEven64 += p512_22; TempEven64 -= p512_08;
    /* expand64_22(26); */
    p512_26 = TempEven64 + r64_1(p512_11) + r64_2(p512_13)
        + r64_3(p512_15) + r64_4(p512_17)
        + r64_5(p512_19) + r64_6(p512_21)
        + r64_7(p512_23) + s64_4(p512_24) + s64_5(p512_25)
        + ((td64_10 + td64_13 - td64_04 + 0x8aaaaaaaaaaaaaa2) ^ p512[1]);
    XH64 ^= p512_26;
    TempOdd64 += p512_23; TempOdd64 -= p512_09;
    /* expand64_21(27); */
    p512_27 = TempOdd64 + r64_1(p512_12) + r64_2(p512_14)
        + r64_3(p512_16) + r64_4(p512_18)
        + r64_5(p512_20) + r64_6(p512_22)
        + r64_7(p512_24) + s64_4(p512_25) + s64_5(p512_26)
        + ((td64_11 + td64_14 - td64_05 + 0x8ffffffffffffff7) ^ p512[2]);
    XH64 ^= p512_27;
    TempEven64 += p512_24; TempEven64 -= p512_10;
    /* expand64_22(28); */
    p512_28 = TempEven64 + r64_1(p512_13) + r64_2(p512_15)
        + r64_3(p512_17) + r64_4(p512_19)
        + r64_5(p512_21) + r64_6(p512_23)
        + r64_7(p512_25) + s64_4(p512_26) + s64_5(p512_27)
        + ((td64_12 + td64_15 - td64_06 + 0x955555555555554c) ^ p512[3]);
    XH64 ^= p512_28;
    TempOdd64 += p512_25; TempOdd64 -= p512_11;
    /* expand64_21(29); */
    p512_29 = TempOdd64 + r64_1(p512_14) + r64_2(p512_16)
        + r64_3(p512_18) + r64_4(p512_20)
        + r64_5(p512_22) + r64_6(p512_24)
        + r64_7(p512_26) + s64_4(p512_27) + s64_5(p512_28)
        + ((td64_13 + td64_00 - td64_07 + 0x9aaaaaaaaaaaaaa1) ^ p512[4]);
    XH64 ^= p512_29;
    TempEven64 += p512_26; TempEven64 -= p512_12;
    /* expand64_22(30); */
    p512_30 = TempEven64 + r64_1(p512_15) + r64_2(p512_17)
        + r64_3(p512_19) + r64_4(p512_21)
        + r64_5(p512_23) + r64_6(p512_25)
        + r64_7(p512_27) + s64_4(p512_28) + s64_5(p512_29)
        + ((td64_14 + td64_01 - td64_08 + 0x9ffffffffffffff6) ^ p512[5]);
    XH64 ^= p512_30;
    TempOdd64 += p512_27; TempOdd64 -= p512_13;
    /* expand64_21(31); */
    p512_31 = TempOdd64 + r64_1(p512_16) + r64_2(p512_18)
        + r64_3(p512_20) + r64_4(p512_22)
        + r64_5(p512_24) + r64_6(p512_26)
        + r64_7(p512_28) + s64_4(p512_29) + s64_5(p512_30)
        + ((td64_15 + td64_02 - td64_09 + 0xa55555555555554b) ^ p512[6]);
    XH64 ^= p512_31;

    /* Compute the chaining pipe for the next message block. */
    p512[0] = (shl(XH64,  5) ^ shr(p512_16, 5) ^ data64[0]) + (XL64 ^ p512_24 ^ p512_00);
    p512[1] = (shr(XH64,  7) ^ shl(p512_17, 8) ^ data64[1]) + (XL64 ^ p512_25 ^ p512_01);
    p512[2] = (shr(XH64,  5) ^ shl(p512_18, 5) ^ data64[2]) + (XL64 ^ p512_26 ^ p512_02);
    p512[3] = (shr(XH64,  1) ^ shl(p512_19, 5) ^ data64[3]) + (XL64 ^ p512_27 ^ p512_03);
    p512[4] = (shr(XH64,  3) ^     p512_20     ^ data64[4]) + (XL64 ^ p512_28 ^ p512_04);
    p512[5] = (shl(XH64,  6) ^ shr(p512_21, 6) ^ data64[5]) + (XL64 ^ p512_29 ^ p512_05);
    p512[6] = (shr(XH64,  4) ^ shl(p512_22, 6) ^ data64[6]) + (XL64 ^ p512_30 ^ p512_06);
    p512[7] = (shr(XH64, 11) ^ shl(p512_23, 2) ^ data64[7]) + (XL64 ^ p512_31 ^ p512_07);

    p512[ 8] = rotl64(p512[4],  9) + (XH64 ^ p512_24 ^ data64[ 8]) + (shl(XL64, 8) ^ p512_23 ^ p512_08);
    p512[ 9] = rotl64(p512[5], 10) + (XH64 ^ p512_25 ^ data64[ 9]) + (shr(XL64, 6) ^ p512_16 ^ p512_09);
    p512[10] = rotl64(p512[6], 11) + (XH64 ^ p512_26 ^ data64[10]) + (shl(XL64, 6) ^ p512_17 ^ p512_10);
    p512[11] = rotl64(p512[7], 12) + (XH64 ^ p512_27 ^ data64[11]) + (shl(XL64, 4) ^ p512_18 ^ p512_11);
    p512[12] = rotl64(p512[0], 13) + (XH64 ^ p512_28 ^ data64[12]) + (shr(XL64, 3) ^ p512_19 ^ p512_12);
    p512[13] = rotl64(p512[1], 14) + (XH64 ^ p512_29 ^ data64[13]) + (shr(XL64, 4) ^ p512_20 ^ p512_13);
    p512[14] = rotl64(p512[2], 15) + (XH64 ^ p512_30 ^ data64[14]) + (shr(XL64, 7) ^ p512_21 ^ p512_14);
    p512[15] = rotl64(p512[3], 16) + (XH64 ^ p512_31 ^ data64[15]) + (shr(XL64, 2) ^ p512_22 ^ p512_15);
}

static void FinalCompression512(uint64 *p512)
{
    uint64 XL64, XH64, TempEven64, TempOdd64;
    uint64 p512_00, p512_01, p512_02, p512_03, p512_04, p512_05, p512_06, p512_07;
    uint64 p512_08, p512_09, p512_10, p512_11, p512_12, p512_13, p512_14, p512_15;
    uint64 p512_16, p512_17, p512_18, p512_19, p512_20, p512_21, p512_22, p512_23;
    uint64 p512_24, p512_25, p512_26, p512_27, p512_28, p512_29, p512_30, p512_31;
    uint64 t512_16, t512_17, t512_18, t512_19, t512_20, t512_21;
    uint64 td64_00, td64_01, td64_02, td64_03, td64_04, td64_05, td64_06, td64_07;
    uint64 td64_08, td64_09, td64_10, td64_11, td64_12, td64_13, td64_14, td64_15;

    /* Mix the message block with the previous pipe. */
    p512_00 = p512[ 0] ^ 0xaaaaaaaaaaaaaaa0;
    p512_01 = p512[ 1] ^ 0xaaaaaaaaaaaaaaa1;
    p512_02 = p512[ 2] ^ 0xaaaaaaaaaaaaaaa2;
    p512_03 = p512[ 3] ^ 0xaaaaaaaaaaaaaaa3;
    p512_04 = p512[ 4] ^ 0xaaaaaaaaaaaaaaa4;
    p512_05 = p512[ 5] ^ 0xaaaaaaaaaaaaaaa5;
    p512_06 = p512[ 6] ^ 0xaaaaaaaaaaaaaaa6;
    p512_07 = p512[ 7] ^ 0xaaaaaaaaaaaaaaa7;
    p512_08 = p512[ 8] ^ 0xaaaaaaaaaaaaaaa8;
    p512_09 = p512[ 9] ^ 0xaaaaaaaaaaaaaaa9;
    p512_10 = p512[10] ^ 0xaaaaaaaaaaaaaaaa;
    p512_11 = p512[11] ^ 0xaaaaaaaaaaaaaaab;
    p512_12 = p512[12] ^ 0xaaaaaaaaaaaaaaac;
    p512_13 = p512[13] ^ 0xaaaaaaaaaaaaaaad;
    p512_14 = p512[14] ^ 0xaaaaaaaaaaaaaaae;
    p512_15 = p512[15] ^ 0xaaaaaaaaaaaaaaaf;

    /* This is the tweak */
    td64_00 = rotl64(p512[ 0],  1);
    td64_01 = rotl64(p512[ 1],  2);
    td64_02 = rotl64(p512[ 2],  3);
    td64_03 = rotl64(p512[ 3],  4);
    td64_04 = rotl64(p512[ 4],  5);
    td64_05 = rotl64(p512[ 5],  6);
    td64_06 = rotl64(p512[ 6],  7);
    td64_07 = rotl64(p512[ 7],  8);
    td64_08 = rotl64(p512[ 8],  9);
    td64_09 = rotl64(p512[ 9], 10);
    td64_10 = rotl64(p512[10], 11);
    td64_11 = rotl64(p512[11], 12);
    td64_12 = rotl64(p512[12], 13);
    td64_13 = rotl64(p512[13], 14);
    td64_14 = rotl64(p512[14], 15);
    td64_15 = rotl64(p512[15], 16);

    t512_16 = p512_01 - p512_14;
    t512_17 = p512_15 - p512_12;
    t512_18 = p512_14 - p512_07;
    t512_19 = p512_13 - p512_06;
    t512_20 = p512_08 - p512_05;
    t512_21 = p512_08 - p512_01;

    p512_16 = p512_05 + t512_18 + p512_10 + p512_13;
    p512_16 = s64_0(p512_16) + 0xaaaaaaaaaaaaaaa1;
    p512_17 = p512_06 - p512_08 + p512_11 + p512_14 - p512_15;
    p512_17 = s64_1(p512_17) + 0xaaaaaaaaaaaaaaa2;
    p512_18 = p512_00 + p512_07 + p512_09 + t512_17;
    p512_18 = s64_2(p512_18) + 0xaaaaaaaaaaaaaaa3;
    p512_19 = p512_00 + t512_21 - p512_10 + p512_13;
    p512_19 = s64_3(p512_19) + 0xaaaaaaaaaaaaaaa4;
    p512_20 = t512_16 + p512_02 + p512_09 - p512_11;
    p512_20 = s64_4(p512_20) + 0xaaaaaaaaaaaaaaa5;
    p512_21 = p512_03 - p512_02 + p512_10 + t512_17;
    p512_21 = s64_0(p512_21) + 0xaaaaaaaaaaaaaaa6;
    p512_22 = p512_04 - p512_00 - p512_03 - p512_11 + p512_13;
    p512_22 = s64_1(p512_22) + 0xaaaaaaaaaaaaaaa7;
    p512_23 = t512_16 - p512_04 - p512_05 - p512_12;
    p512_23 = s64_2(p512_23) + 0xaaaaaaaaaaaaaaa8;
    p512_24 = p512_02 - p512_05 + t512_19 - p512_15;
    p512_24 = s64_3(p512_24) + 0xaaaaaaaaaaaaaaa9;
    p512_25 = p512_00 - p512_03 + p512_06 + t512_18;
    p512_25 = s64_4(p512_25) + 0xaaaaaaaaaaaaaaaa;
    p512_26 = t512_21 - p512_04 - p512_07 + p512_15;
    p512_26 = s64_0(p512_26) + 0xaaaaaaaaaaaaaaab;
    p512_27 = t512_20 - p512_00 - p512_02 + p512_09;
    p512_27 = s64_1(p512_27) + 0xaaaaaaaaaaaaaaac;
    p512_28 = p512_01 + p512_03 - p512_06 - p512_09 + p512_10;
    p512_28 = s64_2(p512_28) + 0xaaaaaaaaaaaaaaad;
    p512_29 = p512_02 + p512_04 + p512_07 + p512_10 + p512_11;
    p512_29 = s64_3(p512_29) + 0xaaaaaaaaaaaaaaae;
    p512_30 = p512_03 + t512_20 - p512_11 - p512_12;
    p512_14 = s64_4(p512_30) + 0xaaaaaaaaaaaaaaaf;
    p512_31 = p512_12 - p512_04 + t512_19 - p512_09;
    p512_15 = s64_0(p512_31) + 0xaaaaaaaaaaaaaaa0;

    p512_00 = p512_16;
    p512_01 = p512_17;
    p512_02 = p512_18;
    p512_03 = p512_19;
    p512_04 = p512_20;
    p512_05 = p512_21;
    p512_06 = p512_22;
    p512_07 = p512_23;
    p512_08 = p512_24;
    p512_09 = p512_25;
    p512_10 = p512_26;
    p512_11 = p512_27;
    p512_12 = p512_28;
    p512_13 = p512_29;

    /* This is the Message expansion. */
    /* It has 16 rounds.       */
    p512_16 = s64_1(p512_00) + s64_2(p512_01) + s64_3(p512_02) + s64_0(p512_03)
        + s64_1(p512_04) + s64_2(p512_05) + s64_3(p512_06) + s64_0(p512_07)
        + s64_1(p512_08) + s64_2(p512_09) + s64_3(p512_10) + s64_0(p512_11)
        + s64_1(p512_12) + s64_2(p512_13) + s64_3(p512_14) + s64_0(p512_15)
        + ((td64_00 + td64_03 - td64_10 + 0x5555555555555550) ^ 0xaaaaaaaaaaaaaaa7);
    XL64 = p512_16;
    p512_17 = s64_1(p512_01) + s64_2(p512_02) + s64_3(p512_03) + s64_0(p512_04)
        + s64_1(p512_05) + s64_2(p512_06) + s64_3(p512_07) + s64_0(p512_08)
        + s64_1(p512_09) + s64_2(p512_10) + s64_3(p512_11) + s64_0(p512_12)
        + s64_1(p512_13) + s64_2(p512_14) + s64_3(p512_15) + s64_0(p512_16)
        + ((td64_01 + td64_04 - td64_11 + 0x5aaaaaaaaaaaaaa5) ^ 0xaaaaaaaaaaaaaaa8);
    XL64 ^= p512_17;
    TempEven64 = p512_14 + p512_12 + p512_10 + p512_08 + p512_06 + p512_04 + p512_02;
    TempOdd64 = p512_15 + p512_13 + p512_11 + p512_09 + p512_07 + p512_05 + p512_03;

    /* expand64_22(18); */
    p512_18 = TempEven64 + r64_1(p512_03) + r64_2(p512_05)
        + r64_3(p512_07) + r64_4(p512_09)
        + r64_5(p512_11) + r64_6(p512_13)
        + r64_7(p512_15) + s64_4(p512_16) + s64_5(p512_17)
        + ((td64_02 + td64_05 - td64_12 + 0x5ffffffffffffffa) ^ 0xaaaaaaaaaaaaaaa9);
    XL64 ^= p512_18;
    /* expand64_21(19); */
    p512_19 = TempOdd64 + r64_1(p512_04) + r64_2(p512_06)
        + r64_3(p512_08) + r64_4(p512_10)
        + r64_5(p512_12) + r64_6(p512_14)
        + r64_7(p512_16) + s64_4(p512_17) + s64_5(p512_18)
        + ((td64_03 + td64_06 - td64_13 + 0x655555555555554f) ^ 0xaaaaaaaaaaaaaaaa);
    XL64 ^= p512_19;
    TempEven64 = TempEven64 + p512_16 - p512_02;
    /* expand64_22(20); */
    p512_20 = TempEven64 + r64_1(p512_05) + r64_2(p512_07)
        + r64_3(p512_09) + r64_4(p512_11)
        + r64_5(p512_13) + r64_6(p512_15)
        + r64_7(p512_17) + s64_4(p512_18) + s64_5(p512_19)
        + ((td64_04 + td64_07 - td64_14 + 0x6aaaaaaaaaaaaaa4) ^ 0xaaaaaaaaaaaaaaab);
    XL64 ^= p512_20;
    TempOdd64 = TempOdd64 + p512_17 - p512_03;
    /* expand64_21(21); */
    p512_21 = TempOdd64 + r64_1(p512_06) + r64_2(p512_08)
        + r64_3(p512_10) + r64_4(p512_12)
        + r64_5(p512_14) + r64_6(p512_16)
        + r64_7(p512_18) + s64_4(p512_19) + s64_5(p512_20)
        + ((td64_05 + td64_08 - td64_15 + 0x6ffffffffffffff9) ^ 0xaaaaaaaaaaaaaaac);
    XL64 ^= p512_21;
    TempEven64 += p512_18; TempEven64 -= p512_04;
    /* expand64_22(22); */
    p512_22 = TempEven64 + r64_1(p512_07) + r64_2(p512_09)
        + r64_3(p512_11) + r64_4(p512_13)
        + r64_5(p512_15) + r64_6(p512_17)
        + r64_7(p512_19) + s64_4(p512_20) + s64_5(p512_21)
        + ((td64_06 + td64_09 - td64_00 + 0x755555555555554e) ^ 0xaaaaaaaaaaaaaaad);
    XL64 ^= p512_22;
    TempOdd64 += p512_19; TempOdd64 -= p512_05;
    /* expand64_21(23); */
    p512_23 = TempOdd64 + r64_1(p512_08) + r64_2(p512_10)
        + r64_3(p512_12) + r64_4(p512_14)
        + r64_5(p512_16) + r64_6(p512_18)
        + r64_7(p512_20) + s64_4(p512_21) + s64_5(p512_22)
        + ((td64_07 + td64_10 - td64_01 + 0x7aaaaaaaaaaaaaa3) ^ 0xaaaaaaaaaaaaaaae);
    XL64 ^= p512_23;
    TempEven64 += p512_20; TempEven64 -= p512_06;
    /* expand64_22(24); */
    p512_24 = TempEven64 + r64_1(p512_09) + r64_2(p512_11)
        + r64_3(p512_13) + r64_4(p512_15)
        + r64_5(p512_17) + r64_6(p512_19)
        + r64_7(p512_21) + s64_4(p512_22) + s64_5(p512_23)
        + ((td64_08 + td64_11 - td64_02 + 0x7ffffffffffffff8) ^ 0xaaaaaaaaaaaaaaaf);
    XH64 = XL64^p512_24;
    TempOdd64 += p512_21; TempOdd64 -= p512_07;
    /* expand64_21(25); */
    p512_25 = TempOdd64 + r64_1(p512_10) + r64_2(p512_12)
        + r64_3(p512_14) + r64_4(p512_16)
        + r64_5(p512_18) + r64_6(p512_20)
        + r64_7(p512_22) + s64_4(p512_23) + s64_5(p512_24)
        + ((td64_09 + td64_12 - td64_03 + 0x855555555555554d) ^ 0xaaaaaaaaaaaaaaa0);
    XH64 ^= p512_25;
    TempEven64 += p512_22; TempEven64 -= p512_08;
    /* expand64_22(26); */
    p512_26 = TempEven64 + r64_1(p512_11) + r64_2(p512_13)
        + r64_3(p512_15) + r64_4(p512_17)
        + r64_5(p512_19) + r64_6(p512_21)
        + r64_7(p512_23) + s64_4(p512_24) + s64_5(p512_25)
        + ((td64_10 + td64_13 - td64_04 + 0x8aaaaaaaaaaaaaa2) ^ 0xaaaaaaaaaaaaaaa1);
    XH64 ^= p512_26;
    TempOdd64 += p512_23; TempOdd64 -= p512_09;
    /* expand64_21(27); */
    p512_27 = TempOdd64 + r64_1(p512_12) + r64_2(p512_14)
        + r64_3(p512_16) + r64_4(p512_18)
        + r64_5(p512_20) + r64_6(p512_22)
        + r64_7(p512_24) + s64_4(p512_25) + s64_5(p512_26)
        + ((td64_11 + td64_14 - td64_05 + 0x8ffffffffffffff7) ^ 0xaaaaaaaaaaaaaaa2);
    XH64 ^= p512_27;
    TempEven64 += p512_24; TempEven64 -= p512_10;
    /* expand64_22(28); */
    p512_28 = TempEven64 + r64_1(p512_13) + r64_2(p512_15)
        + r64_3(p512_17) + r64_4(p512_19)
        + r64_5(p512_21) + r64_6(p512_23)
        + r64_7(p512_25) + s64_4(p512_26) + s64_5(p512_27)
        + ((td64_12 + td64_15 - td64_06 + 0x955555555555554c) ^ 0xaaaaaaaaaaaaaaa3);
    XH64 ^= p512_28;
    TempOdd64 += p512_25; TempOdd64 -= p512_11;
    /* expand64_21(29); */
    p512_29 = TempOdd64 + r64_1(p512_14) + r64_2(p512_16)
        + r64_3(p512_18) + r64_4(p512_20)
        + r64_5(p512_22) + r64_6(p512_24)
        + r64_7(p512_26) + s64_4(p512_27) + s64_5(p512_28)
        + ((td64_13 + td64_00 - td64_07 + 0x9aaaaaaaaaaaaaa1) ^ 0xaaaaaaaaaaaaaaa4);
    XH64 ^= p512_29;
    TempEven64 += p512_26; TempEven64 -= p512_12;
    /* expand64_22(30); */
    p512_30 = TempEven64 + r64_1(p512_15) + r64_2(p512_17)
        + r64_3(p512_19) + r64_4(p512_21)
        + r64_5(p512_23) + r64_6(p512_25)
        + r64_7(p512_27) + s64_4(p512_28) + s64_5(p512_29)
        + ((td64_14 + td64_01 - td64_08 + 0x9ffffffffffffff6) ^ 0xaaaaaaaaaaaaaaa5);
    XH64 ^= p512_30;
    TempOdd64 += p512_27; TempOdd64 -= p512_13;
    /* expand64_21(31); */
    p512_31 = TempOdd64 + r64_1(p512_16) + r64_2(p512_18)
        + r64_3(p512_20) + r64_4(p512_22)
        + r64_5(p512_24) + r64_6(p512_26)
        + r64_7(p512_28) + s64_4(p512_29) + s64_5(p512_30)
        + ((td64_15 + td64_02 - td64_09 + 0xa55555555555554b) ^ 0xaaaaaaaaaaaaaaa6);
    XH64 ^= p512_31;

    /* Compute the chaining pipe for the next message block. */
    p512[0] = (shl(XH64,  5) ^ shr(p512_16, 5) ^ p512[0]) + (XL64 ^ p512_24 ^ p512_00);
    p512[1] = (shr(XH64,  7) ^ shl(p512_17, 8) ^ p512[1]) + (XL64 ^ p512_25 ^ p512_01);
    p512[2] = (shr(XH64,  5) ^ shl(p512_18, 5) ^ p512[2]) + (XL64 ^ p512_26 ^ p512_02);
    p512[3] = (shr(XH64,  1) ^ shl(p512_19, 5) ^ p512[3]) + (XL64 ^ p512_27 ^ p512_03);
    p512[4] = (shr(XH64,  3) ^     p512_20     ^ p512[4]) + (XL64 ^ p512_28 ^ p512_04);
    p512[5] = (shl(XH64,  6) ^ shr(p512_21, 6) ^ p512[5]) + (XL64 ^ p512_29 ^ p512_05);
    p512[6] = (shr(XH64,  4) ^ shl(p512_22, 6) ^ p512[6]) + (XL64 ^ p512_30 ^ p512_06);
    p512[7] = (shr(XH64, 11) ^ shl(p512_23, 2) ^ p512[7]) + (XL64 ^ p512_31 ^ p512_07);

    p512[ 8] = rotl64(p512[4],  9) + (XH64 ^ p512_24 ^ p512[ 8]) + (shl(XL64, 8) ^ p512_23 ^ p512_08);
    p512[ 9] = rotl64(p512[5], 10) + (XH64 ^ p512_25 ^ p512[ 9]) + (shr(XL64, 6) ^ p512_16 ^ p512_09);
    p512[10] = rotl64(p512[6], 11) + (XH64 ^ p512_26 ^ p512[10]) + (shl(XL64, 6) ^ p512_17 ^ p512_10);
    p512[11] = rotl64(p512[7], 12) + (XH64 ^ p512_27 ^ p512[11]) + (shl(XL64, 4) ^ p512_18 ^ p512_11);
    p512[12] = rotl64(p512[0], 13) + (XH64 ^ p512_28 ^ p512[12]) + (shr(XL64, 3) ^ p512_19 ^ p512_12);
    p512[13] = rotl64(p512[1], 14) + (XH64 ^ p512_29 ^ p512[13]) + (shr(XL64, 4) ^ p512_20 ^ p512_13);
    p512[14] = rotl64(p512[2], 15) + (XH64 ^ p512_30 ^ p512[14]) + (shr(XL64, 7) ^ p512_21 ^ p512_14);
    p512[15] = rotl64(p512[3], 16) + (XH64 ^ p512_31 ^ p512[15]) + (shr(XL64, 2) ^ p512_22 ^ p512_15);
}


void HashInit(HashState *state)
{
    state->processed = 0;
    state->unprocessed = 0;
    memcpy(state->pipe, initialPipe, sizeof(state->pipe));
}

void HashUpdate(HashState *restrict state, const byte *restrict data, size_t size)
{
    if (state->unprocessed)
    {
        if (state->unprocessed + size < BLOCK_SIZE)
        {
            memcpy(state->buffer + state->unprocessed, data, size);
            state->unprocessed += (uint)size;
            return;
        }
        memcpy(state->buffer + state->unprocessed, data,
               BLOCK_SIZE - state->unprocessed);
        data += BLOCK_SIZE - state->unprocessed;
        size -= BLOCK_SIZE - state->unprocessed;
        compression512((const uint64*)state->buffer, state->pipe);
        state->processed += BLOCK_SIZE;
    }

    state->processed += size;
    while (size >= BLOCK_SIZE)
    {
        compression512((const uint64*)data, state->pipe);
        size -= BLOCK_SIZE;
        data += BLOCK_SIZE;
    }
    state->processed -= size;
    state->unprocessed = (uint)size;
    if (size)
    {
        memcpy(state->buffer, data, size);
    }
}

void HashFinal(HashState *restrict state, byte *restrict hash)
{
    size_t dataSize = (state->processed + state->unprocessed) * 8;

    memset(state->buffer + state->unprocessed, 0,
           BLOCK_SIZE - state->unprocessed);
    if (state->unprocessed >= BLOCK_SIZE - sizeof(uint64))
    {
        compression512((const uint64*)state->buffer, state->pipe);
        memset(state->buffer + state->unprocessed, 0, BLOCK_SIZE - 8);
    }
    ((uint64*)state->buffer)[15] = dataSize;
    compression512((const uint64*)state->buffer, state->pipe);

    FinalCompression512(state->pipe);

    memcpy(hash, state->pipe + 8, DIGEST_SIZE);
}

void Hash(const byte *restrict data, size_t size, byte *restrict hash)
{
    HashState state;

    HashInit(&state);
    HashUpdate(&state, data, size);
    HashFinal(&state, hash);
}
