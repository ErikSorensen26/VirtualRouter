/**
 * @file Xxh3.hpp
 * @brief XXH3-64 non-cryptographic hash, for content hashing and staleness checks.
 *
 * Not an authentication primitive — XXH3 is fast and well-distributed but not
 * collision-resistant against an adversary. Use it for things like grammar
 * content hashing (see cli::tree::parser::hashDir), not for HMAC or packet
 * authentication; those stay on security::hmac.
 */

#ifndef DIGEST_XXH3_HPP
#define DIGEST_XXH3_HPP

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#include <bit>
#include <cstring>
#include "utils/ByteUtils.hpp"

namespace security::digest
{
namespace
{
inline void mult64to128(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo)
{
    __uint128_t const p = __uint128_t(a) * b;
    lo = uint64_t(p);
    hi = uint64_t(p >> 64);
}

inline uint64_t mult128Fold64(uint64_t a, uint64_t b)
{
    uint64_t lo, hi;
    mult64to128(a, b, lo, hi);
    return lo ^ hi;
}

inline uint64_t avalanche(uint64_t h)
{
    h ^= h >> 37;
    h *= 0x165667919E3779F9ull;
    h ^= h >> 32;
    return h;
}

inline uint64_t avalanche64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    h *= 0x165667B19E3779F9ull;
    h ^= h >> 32;
    return h;
}

inline uint64_t rrmxmx(uint64_t h, uint64_t len)
{
    h ^= std::rotl(h, 49) ^ std::rotl(h, 24);
    h *= 0x9FB21C651E98DF25ull;
    h ^= (h >> 35) + len;
    h *= 0x9FB21C651E98DF25ull;
    h ^= h >> 28;
    return h;
}

inline uint64_t len1to3(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    uint8_t const c1 = input[0];
    uint8_t const c2 = input[len >> 1];
    uint8_t const c3 = input[len - 1];
    uint32_t const combined = (uint32_t(c1) << 16) | (uint32_t(c2) << 24)
                             | (uint32_t(c3) << 0) | (uint32_t(len) << 8);
    uint32_t lo, hi;
    std::memcpy(&lo, secret, sizeof(uint32_t));
    std::memcpy(&hi, secret + 4, sizeof(uint32_t));
    uint64_t const bitflip = uint64_t(lo ^ hi) + seed;
    return avalanche64(uint64_t(combined) & bitflip);
}

inline uint64_t len4to8(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    seed ^= uint64_t(utils::byteSwap32(uint32_t(seed))) << 32;
    uint32_t input1, input2;
    std::memcpy(&input1, input, sizeof(uint32_t));
    std::memcpy(&input2, input + len - 4, sizeof(uint32_t));

    uint64_t lo, hi;
    std::memcpy(&lo, secret + 8, sizeof(uint64_t));
    std::memcpy(&hi, secret + 16, sizeof(uint64_t));
    uint64_t const bitflip = (lo ^ hi) - seed;
    uint64_t const input64 = uint64_t(input2) + (uint64_t(input1) << 32);
    return rrmxmx(input64 ^ bitflip, len);
}

inline uint64_t len9to16(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    uint64_t v1, v2;

    std::memcpy(&v1, secret + 24, sizeof(uint64_t));
    std::memcpy(&v2, secret + 32, sizeof(uint64_t));
    uint64_t const bitflip1 = (v1 ^ v2) + seed;

    std::memcpy(&v1, secret + 40, sizeof(uint64_t));
    std::memcpy(&v2, secret + 48, sizeof(uint64_t));
    uint64_t const bitflip2 = (v1 ^ v2) - seed;

    std::memcpy(&v1, input, sizeof(uint64_t));
    std::memcpy(&v2, input + len - 8, sizeof(uint64_t));
    uint64_t const inputLo = v1 ^ bitflip1;
    uint64_t const inputHi = v2 ^ bitflip2;
    uint64_t const acc = len + utils::byteSwap64(inputLo) + inputHi + mult128Fold64(inputLo, inputHi);
    return avalanche(acc);
}

inline uint64_t len0to16(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    if (len > 8) return len9to16(input, len, secret, seed);
    if (len >= 4) return len4to8(input, len, secret, seed);
    if (len) return len1to3(input, len, secret, seed);
    uint64_t hi, lo;
    std::memcpy(&lo, secret + 56, sizeof(uint64_t));
    std::memcpy(&hi, secret + 64, sizeof(uint64_t));
    return avalanche64(seed ^ (lo ^ hi));
}

inline uint64_t mix16B(const uint8_t* input, const uint8_t* secret, uint64_t seed)
{
    uint64_t inputLo, inputHi;
    std::memcpy(&inputLo, input, sizeof(uint64_t));
    std::memcpy(&inputHi, input + 8, sizeof(uint64_t));
    uint64_t lo, hi;
    std::memcpy(&lo, secret, sizeof(uint64_t));
    std::memcpy(&hi, secret + 8, sizeof(uint64_t));
    return mult128Fold64(inputLo ^ (lo + seed), inputHi ^ (hi - seed));
}

inline uint64_t len17to128(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    uint64_t acc = len * 0x9E3779B185EBCA87ull;
    if (len > 32)
    {
        if (len > 64)
        {
            if (len > 96)
            {
                acc += mix16B(input + 48, secret + 96, seed);
                acc += mix16B(input + len - 64, secret + 112, seed);
            }
            acc += mix16B(input + 32, secret + 64, seed);
            acc += mix16B(input + len - 48, secret + 80, seed);
        }
        acc += mix16B(input + 16, secret + 32, seed);
        acc += mix16B(input + len - 48, secret + 48, seed);
    }
    acc += mix16B(input, secret, seed);
    acc += mix16B(input + len - 32, secret + 48, seed);
    return avalanche(acc);
}

inline uint64_t len129to240(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    uint64_t acc = len * 0x9E3779B185EBCA87ull;
    int const nbRounds = int(len / 16);
    for (int i = 0; i < 8; ++i)
        acc += mix16B(input + 16 * i, secret + 16 * i, seed);
    acc = avalanche(acc);
    for (int i = 8; i < nbRounds; ++i)
        acc += mix16B(input + 16 * i, secret + 16 * (i - 8) + 3, seed);
    acc += mix16B(input + len - 16, secret + 136 - 17, seed);
    return avalanche(acc);
}

inline uint64_t len0to240(const uint8_t* input, size_t len, const uint8_t* secret, uint64_t seed)
{
    if (len > 128) return len129to240(input, len, secret, seed);
    if (len > 16) return len17to128(input, len, secret, seed);
    return len0to16(input, len, secret, seed);
}

inline void accumulate512(uint64_t acc[8], const uint8_t* input, const uint8_t* secret)
{
#if defined(__AVX2__)
    __m256i* const xacc = reinterpret_cast<__m256i*>(acc);
    for (int i = 0; i < 2; ++i)
    {
        __m256i const dataVec   = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input + 32 * i));
        __m256i const keyVec    = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(secret + 32 * i));
        __m256i const dataKey   = _mm256_xor_si256(dataVec, keyVec);
        __m256i const dataKeyLo = _mm256_srli_epi64(dataKey, 32);
        __m256i const product   = _mm256_mul_epu32(dataKey, dataKeyLo);
        __m256i const dataSwap  = _mm256_shuffle_epi32(dataVec, _MM_SHUFFLE(1, 0, 3, 2));
        __m256i const sum       = _mm256_add_epi64(_mm256_loadu_si256(xacc + i), dataSwap);
        _mm256_storeu_si256(xacc + i, _mm256_add_epi64(product, sum));
    }
#else
    for (int i = 0; i < 8; ++i)
    {
        uint64_t dataVal, dataKey;
        std::memcpy(&dataVal, input + 8 * i, sizeof(uint64_t));
        std::memcpy(&dataKey, secret + 8 * i, sizeof(uint64_t));
        dataKey ^= dataVal;
        acc[i ^ 1] += dataVal;
        acc[i] += uint64_t(uint32_t(dataKey)) * uint64_t(uint32_t(dataKey >> 32));
    }
#endif
}

inline void scrambleAcc(uint64_t acc[8], const uint8_t* secret)
{
#if defined(__AVX2__)
    __m256i* const xacc = reinterpret_cast<__m256i*>(acc);
    __m256i const prime32 = _mm256_set1_epi32(int32_t(0x9E3779B1u));
    for (int i = 0; i < 2; ++i)
    {
        __m256i const accVec    = _mm256_loadu_si256(xacc + i);
        __m256i const shifted   = _mm256_srli_epi64(accVec, 47);
        __m256i const dataVec   = _mm256_xor_si256(accVec, shifted);
        __m256i const keyVec    = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(secret + 32 * i));
        __m256i const dataKey   = _mm256_xor_si256(dataVec, keyVec);
        __m256i const dataKeyHi = _mm256_srli_epi64(dataKey, 32);
        __m256i const prodLo    = _mm256_mul_epu32(dataKey, prime32);
        __m256i const prodHi    = _mm256_mul_epu32(dataKeyHi, prime32);
        _mm256_storeu_si256(xacc + i, _mm256_add_epi64(prodLo, _mm256_slli_epi64(prodHi, 32)));
    }
#else
    for (int i = 0; i < 8; ++i)
    {
        acc[i] ^= acc[i] >> 47;
        uint64_t v;
        std::memcpy(&v, secret + 8 * i, sizeof(uint64_t));
        acc[i] ^= v;
        acc[i] *= 0x9E3779B1u;
    }
#endif
}

inline void accumulateStripes(uint64_t acc[8], const uint8_t* input, const uint8_t* secret, size_t nbStripes)
{
    for (size_t n = 0; n < nbStripes; ++n)
        accumulate512(acc, input + n * 64, secret + n * 8);
}

inline void consumeStripes(uint64_t acc[8], size_t& nbStripesSoFar, size_t nbStripesPerBlock,
                           const uint8_t* input, size_t nbStripes,
                           const uint8_t* secret, size_t secretLimit)
{
    if (nbStripesPerBlock - nbStripesSoFar <= nbStripes)
    {
        size_t const toEnd = nbStripesPerBlock - nbStripesSoFar;
        accumulateStripes(acc, input, secret + nbStripesSoFar * 8, toEnd);
        scrambleAcc(acc, secret + secretLimit);
        accumulateStripes(acc, input + toEnd * 64, secret, nbStripes - toEnd);
        nbStripesSoFar = nbStripes - toEnd;
    }
    else
    {
        accumulateStripes(acc, input, secret + nbStripesSoFar * 8, nbStripes);
        nbStripesSoFar += nbStripes;
    }
}

inline uint64_t mix2Accs(const uint64_t* acc, const uint8_t* secret)
{
    uint64_t lo, hi;
    std::memcpy(&lo, secret, sizeof(uint64_t));
    std::memcpy(&hi, secret + 8, sizeof(uint64_t));
    return mult128Fold64(acc[0] ^ lo, acc[1] ^ hi);
}

inline uint64_t mergeAccs(const uint64_t* acc, const uint8_t* secret, uint64_t start)
{
    uint64_t result = start;
    for (int i = 0; i < 4; ++i)
        result += mix2Accs(acc + 2 * i, secret + 16 * i);
    return avalanche(result);
}
}

/**
 * @brief XXH3-64 one-shot/streaming hash, RFC-free (this is xxHash's own design, not an RFC).
 *
 * Same init/update/final shape as the HMAC digests in @ref Hmac.hpp so it can
 * be driven the same way, but it is not itself suitable for HMAC — it is not
 * a cryptographic MAC construction target, just a fast, well-distributed hash.
 */
struct Xxh3_64
{
    static constexpr size_t DIGEST = 8;
    static constexpr size_t INTERNAL_BUFFER = 256;
    static constexpr size_t STRIPE_LEN = 64;
    static constexpr size_t SECRET_SIZE = 192;
    static constexpr size_t SECRET_CONSUME_RATE = 8;
    static constexpr size_t SECRET_LASTACC_START = 7;
    static constexpr size_t SECRET_MERGEACCS_START = 11;
    static constexpr size_t MIDSIZE_MAX = 240;
    static constexpr size_t NB_STRIPES_PER_BLOCK = (SECRET_SIZE - STRIPE_LEN) / SECRET_CONSUME_RATE;
    static constexpr size_t SECRET_LIMIT = SECRET_SIZE - STRIPE_LEN;

    static constexpr uint8_t kSecret[SECRET_SIZE] = {
        0xb8,0xfe,0x6c,0x39,0x23,0xa4,0x4b,0xbe,0x7c,0x01,0x81,0x2c,0xf7,0x21,0xad,0x1c,
        0xde,0xd4,0x6d,0xe9,0x83,0x90,0x97,0xdb,0x72,0x40,0xa4,0xa4,0xb7,0xb3,0x67,0x1f,
        0xcb,0x79,0xe6,0x4e,0xcc,0xc0,0xe5,0x78,0x82,0x5a,0xd0,0x7d,0xcc,0xff,0x72,0x21,
        0xb8,0x08,0x46,0x74,0xf7,0x43,0x24,0x8e,0xe0,0x35,0x90,0xe6,0x81,0x3a,0x26,0x4c,
        0x3c,0x28,0x52,0xbb,0x91,0xc3,0x00,0xcb,0x88,0xd0,0x65,0x8b,0x1b,0x53,0x2e,0xa3,
        0x71,0x64,0x48,0x97,0xa2,0x0d,0xf9,0x4e,0x38,0x19,0xef,0x46,0xa9,0xde,0xac,0xd8,
        0xa8,0xfa,0x76,0x3f,0xe3,0x9c,0x34,0x3f,0xf9,0xdc,0xbb,0xc7,0xc7,0x0b,0x4f,0x1d,
        0x8a,0x51,0xe0,0x4b,0xcd,0xb4,0x59,0x31,0xc8,0x9f,0x7e,0xc9,0xd9,0x78,0x73,0x64,
        0xea,0xc5,0xac,0x83,0x34,0xd3,0xeb,0xc3,0xc5,0x81,0xa0,0xff,0xfa,0x13,0x63,0xeb,
        0x17,0x0d,0xdd,0x51,0xb7,0xf0,0xda,0x49,0xd3,0x16,0x55,0x26,0x29,0xd4,0x68,0x9e,
        0x2b,0x16,0xbe,0x58,0x7d,0x47,0xa1,0xfc,0x8f,0xf8,0xb8,0xd1,0x7a,0xd0,0x31,0xce,
        0x45,0xcb,0x3a,0x8f,0x95,0x16,0x04,0x28,0xaf,0xd7,0xfb,0xca,0xbb,0x4b,0x40,0x7e,
    };

    static constexpr uint64_t kInitAcc[8] = {
        0xC2B2AE3Dull, 0x9E3779B185EBCA87ull, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull,
        0x85EBCA77C2B2AE63ull, 0x85EBCA77ull, 0x27D4EB2F165667C5ull, 0x9E3779B1ull
    };

    uint64_t seed = 0;
    uint64_t acc[8]{};
    uint8_t secretBuf[SECRET_SIZE]{};
    uint8_t buf[INTERNAL_BUFFER]{};
    size_t bufferedSize = 0;
    uint64_t totalLen = 0;
    size_t nbStripesSoFar = 0;

    void init(uint64_t seedIn = 0)
    {
        seed = seedIn;
        std::memcpy(acc, kInitAcc, sizeof(acc));
        if (seed == 0)
        {
            std::memcpy(secretBuf, kSecret, SECRET_SIZE);
        }
        else
        {
            for (size_t i = 0; i < SECRET_SIZE / 16; ++i)
            {
                uint64_t val;

                std::memcpy(&val, kSecret + i * 16, sizeof(uint64_t));
                val += seed;
                std::memcpy(secretBuf + i * 16, &val, sizeof(uint64_t));

                std::memcpy(&val, kSecret + i * 16 + 8, sizeof(uint64_t));
                val -= seed;
                std::memcpy(secretBuf + i * 16 + 8, &val, sizeof(uint64_t));
            }
        }
        bufferedSize = 0;
        totalLen = 0;
        nbStripesSoFar = 0;
    }

    void update(const uint8_t* input, size_t len)
    {
        totalLen += len;
        if (len == 0) return;

        size_t const available = bufferedSize + len;
        if (available <= INTERNAL_BUFFER)
        {
            std::memcpy(buf + bufferedSize, input, len);
            bufferedSize = available;
            return;
        }

        size_t const holdBack = ((available - 1) % INTERNAL_BUFFER) + 1;
        size_t const toConsume = available - holdBack;
        size_t consumed = 0;

        if (bufferedSize > 0)
        {
            size_t const need = INTERNAL_BUFFER - bufferedSize;
            size_t const take = need < len ? need : len;
            std::memcpy(buf + bufferedSize, input, take);
            bufferedSize += take;
            input += take;
            len -= take;
            if (bufferedSize == INTERNAL_BUFFER)
            {
                consumeStripes(acc, nbStripesSoFar, NB_STRIPES_PER_BLOCK,
                               buf, INTERNAL_BUFFER / STRIPE_LEN,
                               secretBuf, SECRET_LIMIT);
                consumed += INTERNAL_BUFFER;
                bufferedSize = 0;
            }
        }

        while (consumed < toConsume)
        {
            consumeStripes(acc, nbStripesSoFar, NB_STRIPES_PER_BLOCK,
                           input, INTERNAL_BUFFER / STRIPE_LEN,
                           secretBuf, SECRET_LIMIT);
            std::memcpy(buf + sizeof(buf) - STRIPE_LEN, input + INTERNAL_BUFFER - STRIPE_LEN, STRIPE_LEN);
            input += INTERNAL_BUFFER;
            len -= INTERNAL_BUFFER;
            consumed += INTERNAL_BUFFER;
        }

        std::memcpy(buf, input, len);
        bufferedSize = len;
    }

    void digestLong(uint64_t out[8]) const
    {
        if (bufferedSize >= STRIPE_LEN)
        {
            size_t const nbStripes = (bufferedSize - 1) / STRIPE_LEN;
            size_t nbSoFar = nbStripesSoFar;
            consumeStripes(out, nbSoFar, NB_STRIPES_PER_BLOCK, buf, nbStripes, secretBuf, SECRET_LIMIT);
            accumulate512(out, buf + bufferedSize - STRIPE_LEN,
                               secretBuf + SECRET_LIMIT - SECRET_LASTACC_START);
        } else {
            uint8_t lastStripe[STRIPE_LEN];
            size_t const catchup = STRIPE_LEN - bufferedSize;
            std::memcpy(lastStripe, buf + sizeof(buf) - catchup, catchup);
            std::memcpy(lastStripe + catchup, buf, bufferedSize);
            accumulate512(out, lastStripe, secretBuf + SECRET_LIMIT - SECRET_LASTACC_START);
        }
    }

    uint64_t digestValue() const
    {
        if (totalLen > MIDSIZE_MAX) {
            uint64_t finalAcc[8];
            std::memcpy(finalAcc, acc, sizeof(finalAcc));
            digestLong(finalAcc);
            return mergeAccs(finalAcc, secretBuf + SECRET_MERGEACCS_START, totalLen * 0x9E3779B185EBCA87ull);
        }
        return len0to240(buf, size_t(totalLen), kSecret, seed);
    }

    void final(uint8_t out[DIGEST])
    {
        const uint64_t v = digestValue();
        for (size_t i = 0; i < DIGEST; ++i)
            out[i] = static_cast<uint8_t>(v >> (8 * i));
    }
};
}

#endif // DIGEST_XXH3_HPP
