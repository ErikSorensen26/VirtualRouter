/**
 * @file Encryption.hpp
 * @brief Stateless HMAC authentication and symmetric cipher helpers for routing protocol security.
 */

#ifndef ENCRYPTION_HPP
#define ENCRYPTION_HPP

#include <openssl/hmac.h>     // HMAC (MD5, SHA)
#include <openssl/md5.h>      // MD5
#include <openssl/sha.h>      // SHA1, SHA256, SHA512
#include <openssl/evp.h>      // AES
#include <openssl/des.h>      // DES/3DES
#include <openssl/evp.h>      // Updated API for cryptographic operations
#include <cstring>

/**
 * @namespace security
 * @brief Cryptographic primitives and checksum utilities used across routing protocol security.
 */
namespace security
{

/**
 * @namespace security::authentication
 * @brief HMAC generation and symmetric encryption/decryption for protocol authentication.
 *
 * All functions are stateless free functions backed by OpenSSL. They operate
 * on raw byte buffers and impose no allocation overhead, making them suitable
 * for use in packet processing paths.
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
    MD5    = MD5_DIGEST_LENGTH,    ///< 16-byte HMAC-MD5; supported by OSPF and EIGRP classic auth.
    SHA1   = SHA_DIGEST_LENGTH,    ///< 20-byte HMAC-SHA-1.
    SHA224 = SHA224_DIGEST_LENGTH, ///< 28-byte HMAC-SHA-224.
    SHA256 = SHA256_DIGEST_LENGTH, ///< 32-byte HMAC-SHA-256.
    SHA384 = SHA384_DIGEST_LENGTH, ///< 48-byte HMAC-SHA-384.
    SHA512 = SHA512_DIGEST_LENGTH  ///< 64-byte HMAC-SHA-512.
};

static constexpr int AES_BLOCK_SIZE = 16;    ///< AES block size in bytes (128 bits).
static constexpr int DES_BLOCK_SIZE = 8;     ///< DES block size in bytes (64 bits).

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
    const EVP_MD *md = nullptr;

    switch (algorithm)
    {
        case HmacType::MD5: md = EVP_md5(); break;
        case HmacType::SHA1: md = EVP_sha1(); break;
        case HmacType::SHA224: md = EVP_sha224(); break;
        case HmacType::SHA256: md = EVP_sha256(); break;
        case HmacType::SHA384: md = EVP_sha384(); break;
        case HmacType::SHA512: md = EVP_sha512(); break;
    }

    unsigned int len = 0;
    HMAC(md, key, keySize, data, dataSize, out, &len);
    return out;
}

/**
 * @brief Encrypts @p in with AES-128-CBC and writes ciphertext to @p out.
 *
 * Output length is rounded up to the nearest AES block boundary
 * (@ref AES_BLOCK_SIZE bytes) due to PKCS#7 padding. @p out must therefore
 * be at least `inLen + AES_BLOCK_SIZE` bytes to accommodate worst-case padding.
 *
 * @param out    Destination for ciphertext; must not overlap @p in.
 * @param in     Plaintext input.
 * @param inLen  Length of @p in in bytes.
 * @param key    16-byte AES-128 key.
 * @param iv     16-byte initialization vector; should be unique per message.
 * @return Number of ciphertext bytes written to @p out.
 *
 * @warning @p iv must not be reused with the same @p key; IV reuse breaks CBC
 *          confidentiality guarantees.
 */
inline static size_t encryptAES(uint8_t* out, const uint8_t* in, size_t inLen, const uint8_t* key, const uint8_t* iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0, outLen = 0;

    EVP_EncryptInit(ctx, EVP_aes_128_cbc(), key, iv);
    EVP_EncryptUpdate(ctx, out, &len, in, inLen);
    outLen = len;
    EVP_EncryptFinal(ctx, out + len, &len);
    outLen += len;

    EVP_CIPHER_CTX_free(ctx);
    return outLen;
}

/**
 * @brief Decrypts AES-128-CBC ciphertext in @p in and writes plaintext to @p out.
 *
 * @param out    Destination for plaintext; must be at least @p inLen bytes.
 * @param in     Ciphertext input produced by @ref encryptAES.
 * @param inLen  Length of @p in in bytes (must be a multiple of @ref AES_BLOCK_SIZE).
 * @param key    16-byte AES-128 key matching the one used for encryption.
 * @param iv     16-byte initialization vector matching the one used for encryption.
 * @return Number of plaintext bytes written to @p out.
 */
inline static size_t decryptAES(uint8_t* out, const uint8_t* in, size_t inLen, const uint8_t* key, const uint8_t* iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0, outLen = 0;

    EVP_DecryptInit(ctx, EVP_aes_128_cbc(), key, iv);
    EVP_DecryptUpdate(ctx, out, &len, in, inLen);
    outLen = len;
    EVP_DecryptFinal(ctx, out + len, &len);
    outLen += len;

    EVP_CIPHER_CTX_free(ctx);
    return outLen;
}

/**
 * @brief Encrypts @p in with single DES-CBC and writes ciphertext to @p out.
 *
 * Output is padded to the nearest DES block boundary (@ref DES_BLOCK_SIZE bytes).
 * @p out must be at least `inLen + DES_BLOCK_SIZE` bytes.
 *
 * @param out    Destination for ciphertext.
 * @param in     Plaintext input.
 * @param inLen  Length of @p in in bytes.
 * @param key    8-byte DES key.
 * @param iv     8-byte initialization vector.
 * @return Number of ciphertext bytes written to @p out.
 *
 * @warning Single DES provides only 56-bit effective key strength and is
 *          considered cryptographically weak; prefer 3DES or AES when possible.
 */
inline static size_t encryptDES(uint8_t* out, const uint8_t* in, size_t inLen, const uint8_t* key, const uint8_t* iv)
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0, outLen = 0;

    EVP_EncryptInit(ctx, EVP_des_cbc(), key, iv);
    EVP_EncryptUpdate(ctx, out, &len, in, inLen);
    outLen = len;
    EVP_EncryptFinal(ctx, out + len, &len);
    outLen += len;

    EVP_CIPHER_CTX_free(ctx);
    return outLen;
}

/**
 * @brief Decrypts single DES-CBC ciphertext in @p in and writes plaintext to @p out.
 *
 * @param out    Destination for plaintext.
 * @param in     Ciphertext input produced by @ref encryptDES.
 * @param inLen  Length of @p in in bytes (must be a multiple of @ref DES_BLOCK_SIZE).
 * @param key    8-byte DES key matching the one used for encryption.
 * @param iv     8-byte initialization vector matching the one used for encryption.
 * @return Number of plaintext bytes written to @p out.
 */
inline static size_t decryptDES(uint8_t* out, const uint8_t* in, size_t inLen, const uint8_t* key, const uint8_t* iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0, outLen = 0;

    EVP_DecryptInit(ctx, EVP_des_cbc(), key, iv);
    EVP_DecryptUpdate(ctx, out, &len, in, inLen);
    outLen = len;
    EVP_DecryptFinal(ctx, out + len, &len);
    outLen += len;

    EVP_CIPHER_CTX_free(ctx);
    return outLen;
}

/**
 * @brief Encrypts @p in with Triple-DES EDE-CBC (3DES) and writes ciphertext to @p out.
 *
 * Uses the three-key EDE (encrypt-decrypt-encrypt) variant. Output is padded
 * to the nearest DES block boundary; @p out must be at least
 * `inLen + DES_BLOCK_SIZE` bytes.
 *
 * @param out    Destination for ciphertext.
 * @param in     Plaintext input.
 * @param inLen  Length of @p in in bytes.
 * @param key    24-byte 3DES key (three 8-byte sub-keys concatenated).
 * @param iv     8-byte initialization vector; should be unique per message.
 * @return Number of ciphertext bytes written to @p out.
 */
inline static size_t encrypt3DES(uint8_t* out, const uint8_t* in, size_t inLen, const uint8_t* key, const uint8_t* iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0, outLen = 0;

    EVP_EncryptInit(ctx, EVP_des_ede3_cbc(), key, iv);
    EVP_EncryptUpdate(ctx, out, &len, in, inLen);
    outLen = len;
    EVP_EncryptFinal(ctx, out + len, &len);
    outLen += len;

    EVP_CIPHER_CTX_free(ctx);
    return outLen;
}

/**
 * @brief Decrypts 3DES-EDE-CBC ciphertext in @p in and writes plaintext to @p out.
 *
 * @param out    Destination for plaintext.
 * @param in     Ciphertext input produced by @ref encrypt3DES.
 * @param inLen  Length of @p in in bytes (must be a multiple of @ref DES_BLOCK_SIZE).
 * @param key    24-byte 3DES key matching the one used for encryption.
 * @param iv     8-byte initialization vector matching the one used for encryption.
 * @return Number of plaintext bytes written to @p out.
 */
inline static size_t decrypt3DES(uint8_t* out, const uint8_t* in, size_t inLen, const uint8_t* key, const uint8_t* iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len = 0, outLen = 0;

    EVP_DecryptInit(ctx, EVP_des_ede3_cbc(), key, iv);
    EVP_DecryptUpdate(ctx, out, &len, in, inLen);
    outLen = len;
    EVP_DecryptFinal(ctx, out + len, &len);
    outLen += len;

    EVP_CIPHER_CTX_free(ctx);
    return outLen;
}

} // namespace authentication

} // namespace security

#endif // ENCRYPTION_HPP
