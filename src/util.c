#include "common.h"
#include "util.h"

static const char base32[] =
{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '2', '3', '4', '5', '6', '7'
};


uint UtilHashString(const char *string, size_t length)
{
    const char *limit = string + length;
    uint hash = 1;
    while (string < limit)
    {
        assert(*string);
        hash = (uint)31 * hash + (uint)*string++;
    }
    return hash;
}

static pure char hex(byte b)
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
        *output++ = hex(*data++ & 4);
    }
}

void UtilBase32(const byte *data, size_t size, char *output)
{
    byte b1, b2, b3, b4, b5;
    assert(size % 5 == 0);
    while (size)
    {
        size -= 5;
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
