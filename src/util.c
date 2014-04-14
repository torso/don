#include "common.h"
#include "util.h"

static const char base32[] =
{
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
    'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', '2', '3', '4', '5', '6', '7'
};


static pureconst char hex(byte b)
{
    if (b >= 10)
    {
        return (char)('a' - 10 + b);
    }
    return (char)('0' + b);
}

void UtilHexString(const byte *data, size_t size, char *output)
{
    while (size--)
    {
        *output++ = hex(*data >> 4);
        *output++ = hex(*data++ & 0xf);
    }
}

void UtilBase32(const byte *data, int size, char *output)
{
    byte b1, b2, b3, b4, b5;
    assert(size % 5 == 0);
    for (; size; size -= 5)
    {
        b1 = *data++;
        b2 = *data++;
        b3 = *data++;
        b4 = *data++;
        b5 = *data++;
        *output++ = base32[b1 >> 3];
        *output++ = base32[((b1 & 7) << 2) + (b2 >> 6)];
        *output++ = base32[(b2 >> 1) & 0x1f];
        *output++ = base32[((b2 & 1) << 4) + (b3 >> 4)];
        *output++ = base32[((b3 & 0xf) << 1) + (b4 >> 7)];
        *output++ = base32[(b4 >> 2) & 0x1f];
        *output++ = base32[((b4 & 3) << 3) + (b5 >> 5)];
        *output++ = base32[b5 & 0x1f];
    }
}

static pureconst byte decodeBase32Byte(char b)
{
    if (b >= base32[0])
    {
        assert(b <= 'z');
        return (byte)(b - base32[0]);
    }
    assert(b >= '2' && b <= '7');
    return (byte)(b + (byte)(sizeof(base32) / sizeof(*base32)) - 1 -
                  base32[sizeof(base32) / sizeof(*base32) - 1]);
}

void UtilDecodeBase32(const char *data, int size, byte *output)
{
    byte b1, b2, b3, b4, b5, b6, b7, b8;
    assert(size % 8 == 0);
    for (; size; size -= 8)
    {
        b1 = decodeBase32Byte(*data++);
        b2 = decodeBase32Byte(*data++);
        b3 = decodeBase32Byte(*data++);
        b4 = decodeBase32Byte(*data++);
        b5 = decodeBase32Byte(*data++);
        b6 = decodeBase32Byte(*data++);
        b7 = decodeBase32Byte(*data++);
        b8 = decodeBase32Byte(*data++);
        *output++ = (byte)((b1 << 3) + (b2 >> 2));
        *output++ = (byte)((b2 << 6) + (b3 << 1) + (b4 >> 4));
        *output++ = (byte)((b4 << 4) + (b5 >> 1));
        *output++ = (byte)((b5 << 7) + (b6 << 2) + (b7 >> 3));
        *output++ = (byte)((b7 << 5) + b8);
    }
}

size_t UtilCountNewlines(const char *text, size_t length)
{
    size_t newlines = 0;
    while (length--)
    {
        if (*text++ == '\n')
        {
            newlines++;
        }
    }
    return newlines;
}
