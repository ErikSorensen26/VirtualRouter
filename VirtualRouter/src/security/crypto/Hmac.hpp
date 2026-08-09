/**
 * @file Hmac.hpp
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

#ifndef SECURITY_HMAC_HPP
#define SECURITY_HMAC_HPP

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace security::hmac
{
/**
 * @brief HMAC digest algorithm selection, with enumerator values equal to the output length in bytes.
 * @ingroup SECURITY
 *
 * The integer value of each enumerator equals the number of bytes in the
 * corresponding digest, which callers can use to size output buffers without
 * hard-coding magic numbers.
 */
enum class HmacType : int
{
    MD5    = 16, ///< 16-byte HMAC-MD5; supported by OSPF and EIGRP classic auth.
    SHA1   = 20, ///< 20-byte HMAC-SHA-1.
    SHA224 = 28, ///< 28-byte HMAC-SHA-224.
    SHA256 = 32, ///< 32-byte HMAC-SHA-256.
    SHA384 = 48, ///< 48-byte HMAC-SHA-384.
    SHA512 = 64  ///< 64-byte HMAC-SHA-512.
};

// Digest lengths in bytes. These carry the names OpenSSL used so the call sites
// that size buffers with them did not have to change when it was dropped.
static constexpr int MD5_DIGEST_LENGTH    = 16;
static constexpr int SHA_DIGEST_LENGTH    = 20;
static constexpr int SHA1_DIGEST_LENGTH   = 20;
static constexpr int SHA224_DIGEST_LENGTH = 28;
static constexpr int SHA256_DIGEST_LENGTH = 32;
static constexpr int SHA384_DIGEST_LENGTH = 48;
static constexpr int SHA512_DIGEST_LENGTH = 64;

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

#endif // SECURITY_HMAC_HPP