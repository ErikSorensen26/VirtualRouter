/**
 * @file Digest.hpp
 * @brief Stateless non-cryptographic hashing helpers (content hashing, cache
 *        staleness checks) — the security::digest counterpart to
 *        security::authentication's HMAC helpers in Encryption.hpp.
 */

#ifndef SECURITY_DIGEST_HPP
#define SECURITY_DIGEST_HPP

#include <cstdint>
#include <cstddef>

#include "security/crypto/digest/Xxh3.hpp"

/**
 * @namespace security::digest
 * @brief Fast, non-cryptographic hashing for content identity and staleness
 *        checks, as opposed to security::authentication's HMAC primitives.
 *
 * XXH3 is not collision-resistant against an adversary and must not be used
 * for packet authentication or anywhere key material is involved — use
 * security::authentication::generateHMAC for that. It is well suited to
 * things like hashing grammar source files to detect a stale build cache.
 */
namespace security::digest
{

/**
 * @brief Computes the XXH3-64 hash of a single buffer.
 *
 * @param data Input data to hash.
 * @param size Length of @p data in bytes.
 * @param seed Optional seed; two different seeds produce unrelated hashes
 *             of the same input.
 * @return The 64-bit XXH3 hash value.
 */
inline uint64_t xxh3_64(const uint8_t* data, size_t size, uint64_t seed = 0)
{
    Xxh3_64 h;
    h.init(seed);
    h.update(data, size);
    uint8_t out[Xxh3_64::DIGEST];
    h.final(out);

    uint64_t v = 0;
    for (size_t i = 0; i < Xxh3_64::DIGEST; ++i)
        v |= uint64_t(out[i]) << (8 * i);
    return v;
}

} // namespace security::digest

#endif // SECURITY_DIGEST_HPP
