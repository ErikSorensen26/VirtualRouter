// Encryption.hpp

#ifndef ENCRYPTION_HPP
#define ENCRYPTION_HPP

#include <openssl/hmac.h>     // HMAC (MD5, SHA)
#include <openssl/md5.h>      // MD5
#include <openssl/sha.h>      // SHA1, SHA256, SHA512
#include <openssl/evp.h>      // AES
#include <openssl/des.h>      // DES/3DES
#include <openssl/evp.h>      // Updated API for cryptographic operations
#include <cstring>


namespace Authentication 
{
    enum class SHA
    {
        SHA1,
        SHA224,
        SHA256,
        SHA384,
        SHA512
    };

    static constexpr int AES_BLOCK_SIZE = 16;    // AES block size (128 bits)
    static constexpr int DES_BLOCK_SIZE = 8;     // DES block size (64 bits)

    // Generate HMAC with MD5
    inline static uint8_t* generateMD5(uint8_t* out, const uint8_t* data, size_t dataSize, const uint8_t* key, size_t keySize)
    {
        unsigned int len = 0;
        HMAC(EVP_md5(), key, keySize, data, dataSize, out, &len);
        return out;
    }

    // Generate HMAC with SHA1/SHA256/SHA512
    inline static uint8_t* generateHMAC(uint8_t* out, const uint8_t* data, size_t dataSize, const uint8_t* key, size_t keySize, const SHA algorithm)
    {
        const EVP_MD *md = nullptr;

        switch (algorithm)
        {
            case SHA::SHA1: md = EVP_sha1(); break;
            case SHA::SHA224: md = EVP_sha224(); break;
            case SHA::SHA256: md = EVP_sha256(); break;
            case SHA::SHA384: md = EVP_sha384(); break;
            case SHA::SHA512: md = EVP_sha512(); break;
        }

        unsigned int len = 0;
        HMAC(md, key, keySize, data, dataSize, out, &len);
        return out;
    }

    // AES Encryption (128-bit, CBC mode)
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

    // AES Decryption (128-bit, CBC mode)
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

    // DES Encryption (CBC mode)
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

    // DES Decryption (CBC mode)
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

    // 3DES Encryption (CBC mode)
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

    // 3DES Decryption (CBC mode)
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
}

#endif // ENCRYPTION_HPP
