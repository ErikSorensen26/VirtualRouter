
#include <openssl/hmac.h>     // HMAC (MD5, SHA)
#include <openssl/md5.h>      // MD5
#include <openssl/sha.h>      // SHA1, SHA256, SHA512
#include <openssl/evp.h>      // AES
#include <openssl/des.h>      // DES/3DES
#include <openssl/evp.h>      // Updated API for cryptographic operations
#include <string>
#include <stdexcept>
#include <cstring>

namespace Authentication {
    static constexpr int AES_BLOCK_SIZE = 16;    // AES block size (128 bits)
    static constexpr int DES_BLOCK_SIZE = 8;     // DES block size (64 bits)

    // Generate HMAC with MD5
    static std::string GenerateMD5(const std::string &data, const std::string &key) {
        unsigned char result[MD5_DIGEST_LENGTH];
        HMAC(EVP_md5(), key.c_str(), key.size(), reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), result, nullptr);
        return std::string(reinterpret_cast<char*>(result), MD5_DIGEST_LENGTH);
    }

    // Verify MD5 Hash
    static bool VerifyMD5(const std::string &data, const std::string &key, const std::string &receivedHash) {
        std::string calculatedHash = GenerateMD5(data, key);
        return calculatedHash == receivedHash;
    }

    // Generate HMAC with SHA1/SHA256/SHA512
    static std::string GenerateHMAC(const std::string &data, const std::string &key, const std::string &algorithm) {
        const EVP_MD *md = nullptr;

        if (algorithm == "SHA1") {
            md = EVP_sha1();
        } else if (algorithm == "SHA256") {
            md = EVP_sha256();
        } else if (algorithm == "SHA512") {
            md = EVP_sha512();
        } else {
            throw std::invalid_argument("Unsupported algorithm: " + algorithm);
        }

        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int len;
        HMAC(md, key.c_str(), key.size(), reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), result, &len);
        return std::string(reinterpret_cast<char*>(result), len);
    }

    // Verify HMAC (SHA1/SHA256/SHA512)
    static bool VerifyHMAC(const std::string &data, const std::string &key, const std::string &algorithm, const std::string &receivedHash) {
        std::string calculatedHash = GenerateHMAC(data, key, algorithm);
        return calculatedHash == receivedHash;
    }

    // AES Encryption (128-bit, CBC mode)
    static std::string EncryptAES(const std::string &plaintext, const std::string &key, const std::string &iv) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        unsigned char ciphertext[plaintext.size() + AES_BLOCK_SIZE];
        int len, ciphertext_len;

        EVP_EncryptInit(ctx, EVP_aes_128_cbc(), reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()));
        EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char*>(plaintext.c_str()), plaintext.size());
        ciphertext_len = len;

        EVP_EncryptFinal(ctx, ciphertext + len, &len);
        ciphertext_len += len;

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(ciphertext), ciphertext_len);
    }

    // AES Decryption (128-bit, CBC mode)
    static std::string DecryptAES(const std::string &ciphertext, const std::string &key, const std::string &iv) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        unsigned char plaintext[ciphertext.size()];
        int len, plaintext_len;

        EVP_DecryptInit(ctx, EVP_aes_128_cbc(), reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()));
        EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char*>(ciphertext.c_str()), ciphertext.size());
        plaintext_len = len;

        EVP_DecryptFinal(ctx, plaintext + len, &len);
        plaintext_len += len;

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext), plaintext_len);
    }

    // DES Encryption (CBC mode)
    static std::string EncryptDES(const std::string &plaintext, const std::string &key, const std::string &iv) {
        unsigned char ciphertext[plaintext.size() + DES_BLOCK_SIZE];
        int len = plaintext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit(ctx, EVP_des_cbc(), reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()));
        EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char*>(plaintext.c_str()), plaintext.size());
        EVP_EncryptFinal(ctx, ciphertext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(ciphertext), len);
    }

    // DES Decryption (CBC mode)
    static std::string DecryptDES(const std::string &ciphertext, const std::string &key, const std::string &iv) {
        unsigned char plaintext[ciphertext.size()];
        int len = ciphertext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit(ctx, EVP_des_cbc(), reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()));
        EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char*>(ciphertext.c_str()), ciphertext.size());
        EVP_DecryptFinal(ctx, plaintext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext), len);
    }

    // 3DES Encryption (CBC mode)
    static std::string Encrypt3DES(const std::string &plaintext, const std::string &key, const std::string &iv) {
        unsigned char ciphertext[plaintext.size() + DES_BLOCK_SIZE];
        int len = plaintext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit(ctx, EVP_des_ede3_cbc(), reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()));
        EVP_EncryptUpdate(ctx, ciphertext, &len, reinterpret_cast<const unsigned char*>(plaintext.c_str()), plaintext.size());
        EVP_EncryptFinal(ctx, ciphertext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(ciphertext), len);
    }

    // 3DES Decryption (CBC mode)
    static std::string Decrypt3DES(const std::string &ciphertext, const std::string &key, const std::string &iv) {
        unsigned char plaintext[ciphertext.size()];
        int len = ciphertext.size();

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit(ctx, EVP_des_ede3_cbc(), reinterpret_cast<const unsigned char*>(key.c_str()), reinterpret_cast<const unsigned char*>(iv.c_str()));
        EVP_DecryptUpdate(ctx, plaintext, &len, reinterpret_cast<const unsigned char*>(ciphertext.c_str()), ciphertext.size());
        EVP_DecryptFinal(ctx, plaintext + len, &len);

        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext), len);
    }
}
