/**
 * @file Digest.hpp
 * @brief Self-contained MD5, SHA-1 and SHA-2 block functions.
 *
 * These exist so the router carries no crypto dependency. The only primitive
 * the codebase needs is an HMAC over a routing-protocol packet, which is a few
 * hundred bytes at most, and at that size a library call costs more in
 * indirection than the compression function costs to run.
 *
 * Each digest is a @c Ctx struct with the usual init/update/final shape, so
 * @ref security::authentication::generateHMAC can drive any of them through the
 * same three calls. State is plain members and nothing allocates, which keeps
 * the whole HMAC computation on the stack and usable from a packet path.
 *
 * The implementations follow RFC 1321 (MD5), RFC 3174 (SHA-1) and FIPS 180-4
 * (SHA-2). MD5 and SHA-1 are here because OSPF and EIGRP specify them, not
 * because they are sound choices for new work.
 */

#ifndef SECURITY_DIGEST_HPP
#define SECURITY_DIGEST_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace security::digest
{

namespace
{
/// @brief Reads a big-endian word out of a byte buffer.
inline uint32_t loadBE32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

/// @brief Reads a big-endian 64-bit word out of a byte buffer.
inline uint64_t loadBE64(const uint8_t* p)
{
    return (uint64_t(loadBE32(p)) << 32) | loadBE32(p + 4);
}

/// @brief Writes a word to a byte buffer in big-endian order.
inline void storeBE32(uint8_t* p, uint32_t v)
{
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

/// @brief Writes a 64-bit word to a byte buffer in big-endian order.
inline void storeBE64(uint8_t* p, uint64_t v)
{
    storeBE32(p, uint32_t(v >> 32));
    storeBE32(p + 4, uint32_t(v));
}

/// @brief Writes a word to a byte buffer in little-endian order.
inline void storeLE32(uint8_t* p, uint32_t v)
{
    p[0] = uint8_t(v);       p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}

/// @brief Reads a little-endian word out of a byte buffer.
inline uint32_t loadLE32(const uint8_t* p)
{
    return  uint32_t(p[0])        | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}

/**
 * @brief MD5 as specified by RFC 1321.
 *
 * Retained only because OSPFv2 and EIGRP classic authentication specify it.
 * MD5 is collision-broken and must not be used where collision resistance
 * matters; inside HMAC it is weakened but not currently broken.
 */
struct Md5
{
    static constexpr size_t BLOCK  = 64;  ///< Compression function input size.
    static constexpr size_t DIGEST = 16;  ///< Output size in bytes.

    uint32_t h[4]  = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint64_t total = 0;                   ///< Message length in bytes.
    uint8_t  buf[BLOCK]{};                ///< Partial block awaiting more input.
    size_t   used = 0;                    ///< Bytes currently in @c buf.

    static void compress(uint32_t h[4], const uint8_t* p)
    {
        static constexpr uint32_t K[64] = {
            0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
            0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
            0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
            0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
            0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
            0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
            0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
            0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
            0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
            0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
            0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
            0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
            0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
            0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
            0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
            0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

        static constexpr int R[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

        uint32_t m[16];
        for (int i = 0; i < 16; ++i) m[i] = loadLE32(p + i * 4);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];

        for (int i = 0; i < 64; ++i)
        {
            uint32_t f;
            int g;

            if (i < 16)      { f = (b & c) | (~b & d);        g = i; }
            else if (i < 32) { f = (d & b) | (~d & c);        g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d;                 g = (3 * i + 5) % 16; }
            else             { f = c ^ (b | ~d);              g = (7 * i) % 16; }

            const uint32_t tmp = d;
            d = c;
            c = b;
            b = b + std::rotl(a + f + K[i] + m[g], R[i]);
            a = tmp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }

    void init() { *this = Md5{}; }

    void update(const uint8_t* data, size_t len)
    {
        total += len;

        if (used)
        {
            const size_t need = BLOCK - used;
            const size_t take = len < need ? len : need;
            std::memcpy(buf + used, data, take);
            used += take;
            data += take;
            len  -= take;

            if (used < BLOCK) return;
            compress(h, buf);
            used = 0;
        }

        while (len >= BLOCK)
        {
            compress(h, data);
            data += BLOCK;
            len  -= BLOCK;
        }

        if (len)
        {
            std::memcpy(buf, data, len);
            used = len;
        }
    }

    void final(uint8_t* out)
    {
        const uint64_t bits = total * 8;

        // The 0x80 terminator always fits; the length needs the last 8 bytes,
        // so a block with no room for it is flushed first.
        buf[used++] = 0x80;
        if (used > BLOCK - 8)
        {
            std::memset(buf + used, 0, BLOCK - used);
            compress(h, buf);
            used = 0;
        }
        std::memset(buf + used, 0, BLOCK - 8 - used);
        storeLE32(buf + BLOCK - 8, uint32_t(bits));
        storeLE32(buf + BLOCK - 4, uint32_t(bits >> 32));
        compress(h, buf);

        for (int i = 0; i < 4; ++i) storeLE32(out + i * 4, h[i]);
    }
};

/**
 * @brief SHA-1 as specified by RFC 3174.
 *
 * Collision-broken since 2017 and kept only for protocols that specify it.
 */
struct Sha1
{
    static constexpr size_t BLOCK  = 64;
    static constexpr size_t DIGEST = 20;

    uint32_t h[5]  = {0x67452301u, 0xefcdab89u, 0x98badcfeu,
                      0x10325476u, 0xc3d2e1f0u};
    uint64_t total = 0;
    uint8_t  buf[BLOCK]{};
    size_t   used = 0;

    static void compress(uint32_t h[5], const uint8_t* p)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = loadBE32(p + i * 4);
        for (int i = 16; i < 80; ++i)
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;

            if (i < 20)      { f = (b & c) | (~b & d);        k = 0x5a827999u; }
            else if (i < 40) { f = b ^ c ^ d;                 k = 0x6ed9eba1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
            else             { f = b ^ c ^ d;                 k = 0xca62c1d6u; }

            const uint32_t tmp = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = tmp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void init() { *this = Sha1{}; }

    void update(const uint8_t* data, size_t len)
    {
        total += len;

        if (used)
        {
            const size_t need = BLOCK - used;
            const size_t take = len < need ? len : need;
            std::memcpy(buf + used, data, take);
            used += take;
            data += take;
            len  -= take;

            if (used < BLOCK) return;
            compress(h, buf);
            used = 0;
        }

        while (len >= BLOCK)
        {
            compress(h, data);
            data += BLOCK;
            len  -= BLOCK;
        }

        if (len)
        {
            std::memcpy(buf, data, len);
            used = len;
        }
    }

    void final(uint8_t* out)
    {
        const uint64_t bits = total * 8;

        buf[used++] = 0x80;
        if (used > BLOCK - 8)
        {
            std::memset(buf + used, 0, BLOCK - used);
            compress(h, buf);
            used = 0;
        }
        std::memset(buf + used, 0, BLOCK - 8 - used);
        storeBE64(buf + BLOCK - 8, bits);
        compress(h, buf);

        for (int i = 0; i < 5; ++i) storeBE32(out + i * 4, h[i]);
    }
};

/**
 * @brief SHA-224 and SHA-256, sharing one compression function.
 *
 * The two differ only in their initial state and in how much of the final
 * state is emitted, so @p Bytes selects the variant.
 */
template <size_t Bytes>
struct Sha256Base
{
    static_assert(Bytes == 28 || Bytes == 32, "SHA-2 32-bit variants are 224 or 256");

    static constexpr size_t BLOCK  = 64;
    static constexpr size_t DIGEST = Bytes;

    uint32_t h[8] = {};
    uint64_t total = 0;
    uint8_t  buf[BLOCK]{};
    size_t   used = 0;

    Sha256Base() { init(); }

    static void compress(uint32_t h[8], const uint8_t* p)
    {
        static constexpr uint32_t K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = loadBE32(p + i * 4);
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i)
        {
            const uint32_t S1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;

            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void init()
    {
        total = 0;
        used  = 0;

        if constexpr (Bytes == 32)
        {
            const uint32_t iv[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
            std::memcpy(h, iv, sizeof(h));
        }
        else
        {
            const uint32_t iv[8] = {0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u,
                                    0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u};
            std::memcpy(h, iv, sizeof(h));
        }
    }

    void update(const uint8_t* data, size_t len)
    {
        total += len;

        if (used)
        {
            const size_t need = BLOCK - used;
            const size_t take = len < need ? len : need;
            std::memcpy(buf + used, data, take);
            used += take;
            data += take;
            len  -= take;

            if (used < BLOCK) return;
            compress(h, buf);
            used = 0;
        }

        while (len >= BLOCK)
        {
            compress(h, data);
            data += BLOCK;
            len  -= BLOCK;
        }

        if (len)
        {
            std::memcpy(buf, data, len);
            used = len;
        }
    }

    void final(uint8_t* out)
    {
        const uint64_t bits = total * 8;

        buf[used++] = 0x80;
        if (used > BLOCK - 8)
        {
            std::memset(buf + used, 0, BLOCK - used);
            compress(h, buf);
            used = 0;
        }
        std::memset(buf + used, 0, BLOCK - 8 - used);
        storeBE64(buf + BLOCK - 8, bits);
        compress(h, buf);

        // SHA-224 is SHA-256 truncated, so the emitted word count is what
        // distinguishes them here.
        for (size_t i = 0; i < Bytes / 4; ++i) storeBE32(out + i * 4, h[i]);
    }
};

using Sha224 = Sha256Base<28>;
using Sha256 = Sha256Base<32>;

/**
 * @brief SHA-384 and SHA-512, sharing one 64-bit compression function.
 *
 * Same relationship as the 32-bit pair: @p Bytes picks the initial state and
 * how much of the final state is emitted.
 */
template <size_t Bytes>
struct Sha512Base
{
    static_assert(Bytes == 48 || Bytes == 64, "SHA-2 64-bit variants are 384 or 512");

    static constexpr size_t BLOCK  = 128;
    static constexpr size_t DIGEST = Bytes;

    uint64_t h[8] = {};
    uint64_t total = 0;
    uint8_t  buf[BLOCK]{};
    size_t   used = 0;

    Sha512Base() { init(); }

    static void compress(uint64_t h[8], const uint8_t* p)
    {
        static constexpr uint64_t K[80] = {
            0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
            0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
            0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
            0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
            0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
            0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
            0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
            0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
            0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
            0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
            0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
            0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
            0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
            0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
            0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
            0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
            0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
            0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
            0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
            0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};

        uint64_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = loadBE64(p + i * 8);
        for (int i = 16; i < 80; ++i)
        {
            const uint64_t s0 = std::rotr(w[i - 15], 1) ^ std::rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = std::rotr(w[i - 2], 19) ^ std::rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 80; ++i)
        {
            const uint64_t S1 = std::rotr(e, 14) ^ std::rotr(e, 18) ^ std::rotr(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint64_t S0 = std::rotr(a, 28) ^ std::rotr(a, 34) ^ std::rotr(a, 39);
            const uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t t2 = S0 + mj;

            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void init()
    {
        total = 0;
        used  = 0;

        if constexpr (Bytes == 64)
        {
            const uint64_t iv[8] = {
                0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
            std::memcpy(h, iv, sizeof(h));
        }
        else
        {
            const uint64_t iv[8] = {
                0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull, 0x9159015a3070dd17ull, 0x152fecd8f70e5939ull,
                0x67332667ffc00b31ull, 0x8eb44a8768581511ull, 0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull};
            std::memcpy(h, iv, sizeof(h));
        }
    }

    void update(const uint8_t* data, size_t len)
    {
        total += len;

        if (used)
        {
            const size_t need = BLOCK - used;
            const size_t take = len < need ? len : need;
            std::memcpy(buf + used, data, take);
            used += take;
            data += take;
            len  -= take;

            if (used < BLOCK) return;
            compress(h, buf);
            used = 0;
        }

        while (len >= BLOCK)
        {
            compress(h, data);
            data += BLOCK;
            len  -= BLOCK;
        }

        if (len)
        {
            std::memcpy(buf, data, len);
            used = len;
        }
    }

    void final(uint8_t* out)
    {
        const uint64_t bits = total * 8;

        // The length field is 128 bits here, but a routing packet never
        // approaches 2^64 bytes, so the high half is always zero.
        buf[used++] = 0x80;
        if (used > BLOCK - 16)
        {
            std::memset(buf + used, 0, BLOCK - used);
            compress(h, buf);
            used = 0;
        }
        std::memset(buf + used, 0, BLOCK - 16 - used);
        storeBE64(buf + BLOCK - 16, 0);
        storeBE64(buf + BLOCK - 8, bits);
        compress(h, buf);

        for (size_t i = 0; i < Bytes / 8; ++i) storeBE64(out + i * 8, h[i]);
    }
};

using Sha384 = Sha512Base<48>;
using Sha512 = Sha512Base<64>;

/**
 * @brief HMAC as specified by RFC 2104, over any digest in this header.
 *
 * A key longer than the digest's block size is replaced by its own hash, and a
 * shorter one is zero-padded, per the RFC. Both pad buffers are block-sized
 * stack arrays, so the whole computation allocates nothing.
 *
 * @tparam H     Digest type supplying BLOCK, DIGEST, and init/update/final.
 * @param out    Destination for @c H::DIGEST bytes.
 * @param data   Message to authenticate.
 * @param dataSize Length of @p data in bytes.
 * @param key    Secret key.
 * @param keySize Length of @p key in bytes.
 */
template <typename H>
void hmac(uint8_t* out, const uint8_t* data, size_t dataSize,
          const uint8_t* key, size_t keySize)
{
    uint8_t block[H::BLOCK]{};

    if (keySize > H::BLOCK)
    {
        H kh;
        kh.init();
        kh.update(key, keySize);
        kh.final(block);
    }
    else if (keySize)
    {
        std::memcpy(block, key, keySize);
    }

    uint8_t ipad[H::BLOCK];
    uint8_t opad[H::BLOCK];
    for (size_t i = 0; i < H::BLOCK; ++i)
    {
        ipad[i] = uint8_t(block[i] ^ 0x36);
        opad[i] = uint8_t(block[i] ^ 0x5c);
    }

    uint8_t inner[H::DIGEST];
    H h;
    h.init();
    h.update(ipad, H::BLOCK);
    h.update(data, dataSize);
    h.final(inner);

    H o;
    o.init();
    o.update(opad, H::BLOCK);
    o.update(inner, H::DIGEST);
    o.final(out);
}
}

#endif // SECURITY_DIGEST_HPP
