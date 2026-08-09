/**
 * @file Encryption.hpp
 * @brief Stateless HMAC authentication helpers for routing protocol security.
 */

#ifndef ENCRYPTION_HPP
#define ENCRYPTION_HPP

#include <cstring>

#include "security/crypto/Hmac.hpp"
#include "security/crypto/hmac/MD5.hpp"
#include "security/crypto/hmac/SHA.hpp"

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
 * All functions are stateless free functions backed by @ref Hmac.hpp, which
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

// The digest engine lives in security::hmac (see Hmac.hpp); this alias keeps
// the authentication-facing API — HmacType, MD5_DIGEST_LENGTH, etc. — under
// the namespace every protocol call site and KeyChain already spells.
using hmac::HmacType;
using hmac::MD5_DIGEST_LENGTH;
using hmac::SHA_DIGEST_LENGTH;
using hmac::SHA1_DIGEST_LENGTH;
using hmac::SHA224_DIGEST_LENGTH;
using hmac::SHA256_DIGEST_LENGTH;
using hmac::SHA384_DIGEST_LENGTH;
using hmac::SHA512_DIGEST_LENGTH;

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
        case HmacType::MD5:    hmac::hmac<hmac::Md5>          (out, data, dataSize, key, keySize); break;
        case HmacType::SHA1:   hmac::hmac<hmac::sha::Sha1>    (out, data, dataSize, key, keySize); break;
        case HmacType::SHA224: hmac::hmac<hmac::sha::Sha224>  (out, data, dataSize, key, keySize); break;
        case HmacType::SHA256: hmac::hmac<hmac::sha::Sha256>  (out, data, dataSize, key, keySize); break;
        case HmacType::SHA384: hmac::hmac<hmac::sha::Sha384>  (out, data, dataSize, key, keySize); break;
        case HmacType::SHA512: hmac::hmac<hmac::sha::Sha512>  (out, data, dataSize, key, keySize); break;
    }

    return out;
}

} // namespace authentication

} // namespace security

#endif // ENCRYPTION_HPP
