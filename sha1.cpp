#include "sha1.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

using namespace std;

namespace
{
inline uint32_t rol(uint32_t value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

void sha1Transform(array<uint32_t, 5>& state, const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
            | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
            | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
            | static_cast<uint32_t>(block[i * 4 + 3]);
    }

    for (int i = 16; i < 80; ++i)
    {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; ++i)
    {
        uint32_t f = 0;
        uint32_t k = 0;
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        const uint32_t temp = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}
}

string sha1Hex(const string& data)
{
    array<uint32_t, 5> state{
        0x67452301,
        0xEFCDAB89,
        0x98BADCFE,
        0x10325476,
        0xC3D2E1F0};

    const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8;
    size_t offset = 0;
    while (offset + 64 <= data.size())
    {
        sha1Transform(state, reinterpret_cast<const uint8_t*>(data.data() + offset));
        offset += 64;
    }

    uint8_t block[64] = {};
    const size_t remaining = data.size() - offset;
    if (remaining > 0)
    {
        memcpy(block, data.data() + offset, remaining);
    }

    block[remaining] = 0x80;
    if (remaining >= 56)
    {
        sha1Transform(state, block);
        memset(block, 0, sizeof(block));
    }

    for (int i = 0; i < 8; ++i)
    {
        block[63 - i] = static_cast<uint8_t>((bit_len >> (8 * i)) & 0xFF);
    }

    sha1Transform(state, block);

    ostringstream out;
    out << hex << setfill('0');
    for (uint32_t word : state)
    {
        out << setw(8) << word;
    }

    return out.str();
}
