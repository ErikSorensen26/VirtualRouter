/**
 * @file Encryption.hpp
 * @brief Stateless HMAC authentication helpers for routing protocol security.
 */

#ifndef ENCRYPTION_HPP
#define ENCRYPTION_HPP

#include <cstring>

#include "security/Digest.hpp"

/**
 * @namespace security
 * @brief Cryptographic primitives and checksum utilities used across routing protocol security.
 */
namespace security
{

/**
 * @namespace security::authentication
 * @brief HMAC generation for protocol authentication.
 *
 * All functions are stateless free functions backed by @ref Digest.hpp, which
 * implements the digests directly rather than calling out to a crypto library.
 * They operate on raw byte buffers and impose no allocation overhead, making
 * them suitable for use in packet processing paths.
 *
 * Callers are responsible for providing correctly sized output buffers — the
 * required size for each HMAC variant is given by the corresponding
 * @ref HmacType enumerator value.
 */
namespace authentication
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
 * @brief Computes an HMAC digest over @p data using the specified algorithm and key.
 *
 * The output is written directly into @p out, which must be at least
 * `static_cast<int>(algorithm)` bytes wide. The function returns @p out to
 * allow chaining with downstream operations (e.g., a `memcmp` call).
 *
 * @param out       Destination buffer; must be pre-allocated to the digest length.
 * @param data      Input data to authenticate.
 * @param dataSize  Length of @p data in bytes.
 * @param key       HMAC secret key.
 * @param keySize   Length of @p key in bytes.
 * @param algorithm Hash function to use; also determines the required @p out size.
 * @return Pointer to @p out (the completed digest).
 *
 * @warning @p out must not overlap with @p data or @p key.
 */
inline static uint8_t* generateHMAC(uint8_t* out, const uint8_t* data, size_t dataSize, const uint8_t* key, size_t keySize, const HmacType algorithm)
{
    switch (algorithm)
    {
        case HmacType::MD5:    digest::hmac<digest::Md5>   (out, data, dataSize, key, keySize); break;
        case HmacType::SHA1:   digest::hmac<digest::Sha1>  (out, data, dataSize, key, keySize); break;
        case HmacType::SHA224: digest::hmac<digest::Sha224>(out, data, dataSize, key, keySize); break;
        case HmacType::SHA256: digest::hmac<digest::Sha256>(out, data, dataSize, key, keySize); break;
        case HmacType::SHA384: digest::hmac<digest::Sha384>(out, data, dataSize, key, keySize); break;
        case HmacType::SHA512: digest::hmac<digest::Sha512>(out, data, dataSize, key, keySize); break;
    }

    return out;
}

} // namespace authentication

} // namespace security

#endif // ENCRYPTION_HPP
