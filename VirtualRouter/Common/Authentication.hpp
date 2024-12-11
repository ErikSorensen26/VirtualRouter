
#include <openssl/hmac.h>     // HMAC (MD5, SHA)
#include <openssl/md5.h>      // MD5
#include <openssl/sha.h>      // SHA1, SHA256, SHA512
#include <openssl/evp.h>      // AES
#include <openssl/des.h>      // DES/3DES
#include <openssl/evp.h>      // Updated API for cryptographic operations
#include <string>
#include <stdexcept>
#include <cstring>
#include <ByteString.hpp>

namespace Authentication {
    static constexpr int AES_BLOCK_SIZE = 16;    // AES block size (128 bits)
    static constexpr int DES_BLOCK_SIZE = 8;     // DES block size (64 bits)

    // Generate HMAC with MD5
    static ByteString generateMD5(const ByteString &data, const ByteString &key) {
        unsigned char result[MD5_DIGEST_LENGTH];
        HMAC(EVP_md5(), key.toString().c_str(), key.size(), reinterpret_cast<const unsigned char*>(data.toString().c_str()), data.size(), result, nullptr);
        return std::string(reinterpret_cast<char*>(result), MD5_DIGEST_LENGTH);
    }

    // Verify MD5 Hash
    static bool verifyMD5(const ByteString &data, const ByteString &key, const ByteString &receivedHash) {
        ByteString calculatedHash = generateMD5(data, key);
        return calculatedHash == receivedHash;
    }

    // Generate HMAC with SHA1/SHA256/SHA512
    static ByteString generateHMAC(const ByteString &data, const ByteString &key, const ByteString &algorithm) {
        const EVP_MD *md = nullptr;

        if (algorithm == "SHA1") {
            md = EVP_sha1();
        } else if (algorithm == "SHA256") {
            md = EVP_sha256();
        } else if (algorithm == "SHA512") {
            md = EVP_sha512();
        } else {
            throw std::invalid_argument("Unsupported algorithm: " + algorithm.toString());
        }

        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int len;
        HMAC(md, key.toString().c_str(), key.size(), reinterpret_cast<const unsigned char*>(data.toString().c_str()), data.size(), result, &len);
        return std::string(reinterpret_cast<char*>(result), len);
    }

    // Verify HMAC (SHA1/SHA256/SHA512)
    static bool verifyHMAC(const ByteString &data, const ByteString &key, const ByteString &algorithm, const ByteString &receivedHash) {
        ByteString calculatedHash = generateHMAC(data, key, algorithm);
        return calculatedHash == receivedHash;
    }

    // AES Encryption (128-bit, CBC mode)
    static ByteString encryptAES(const ByteString &plaintext, const ByteString &key, const ByteString &iv) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        unsigned char ciphertext[plaintext.size() + AES_BLOCK_SIZE];
        int len, ciphertext_len;

        EVP_EncryptInit(ctx, EVP_aes_128_cbc(), reinterpret_cast<const unsigned char*>(key.toString().c_str()), reinterpret_cast<const unsigned char*>(iv.toString().c_str()));
        EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char*>(plaintext.toString().c_str()), plaintext.size());
        ciphertext_len = len;

        EVP_EncryptFinal(ctx, ciphertext + len, &len);
        ciphertext_len += len;

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(ciphertext), ciphertext_len);
    }

    // AES Decryption (128-bit, CBC mode)
    static ByteString decryptAES(const ByteString &ciphertext, const ByteString &key, const ByteString &iv) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        unsigned char plaintext[ciphertext.size()];
        int len, plaintext_len;

        EVP_DecryptInit(ctx, EVP_aes_128_cbc(), reinterpret_cast<const unsigned char*>(key.toString().c_str()), reinterpret_cast<const unsigned char*>(iv.toString().c_str()));
        EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char*>(ciphertext.toString().c_str()), ciphertext.size());
        plaintext_len = len;

        EVP_DecryptFinal(ctx, plaintext + len, &len);
        plaintext_len += len;

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext), plaintext_len);
    }

    // DES Encryption (CBC mode)
    static ByteString encryptDES(const ByteString &plaintext, const ByteString &key, const ByteString &iv) {
        unsigned char ciphertext[plaintext.size() + DES_BLOCK_SIZE];
        int len = plaintext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit(ctx, EVP_des_cbc(), reinterpret_cast<const unsigned char*>(key.toString().c_str()), reinterpret_cast<const unsigned char*>(iv.toString().c_str()));
        EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char*>(plaintext.toString().c_str()), plaintext.size());
        EVP_EncryptFinal(ctx, ciphertext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(ciphertext), len);
    }

    // DES Decryption (CBC mode)
    static ByteString decryptDES(const ByteString &ciphertext, const ByteString &key, const ByteString &iv) {
        unsigned char plaintext[ciphertext.size()];
        int len = ciphertext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit(ctx, EVP_des_cbc(), reinterpret_cast<const unsigned char*>(key.toString().c_str()), reinterpret_cast<const unsigned char*>(iv.toString().c_str()));
        EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char*>(ciphertext.toString().c_str()), ciphertext.size());
        EVP_DecryptFinal(ctx, plaintext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext), len);
    }

    // 3DES Encryption (CBC mode)
    static ByteString encrypt3DES(const ByteString &plaintext, const ByteString &key, const ByteString &iv) {
        unsigned char ciphertext[plaintext.size() + DES_BLOCK_SIZE];
        int len = plaintext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit(ctx, EVP_des_ede3_cbc(), reinterpret_cast<const unsigned char*>(key.toString().c_str()), reinterpret_cast<const unsigned char*>(iv.toString().c_str()));
        EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char*>(plaintext.toString().c_str()), plaintext.size());
        EVP_EncryptFinal(ctx, ciphertext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(ciphertext), len);
    }

    // 3DES Decryption (CBC mode)
    static ByteString decrypt3DES(const ByteString &ciphertext, const ByteString &key, const ByteString &iv) {
        unsigned char plaintext[ciphertext.size()];
        int len = ciphertext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit(ctx, EVP_des_ede3_cbc(), reinterpret_cast<const unsigned char*>(key.toString().c_str()), reinterpret_cast<const unsigned char*>(iv.toString().c_str()));
        EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char*>(ciphertext.toString().c_str()), ciphertext.size());
        EVP_DecryptFinal(ctx, plaintext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext), len);
    }
}
