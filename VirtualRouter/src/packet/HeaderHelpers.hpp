
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <AddressFamily.hpp>
#include <likely.hpp>

#ifndef HEADER_HELPER_HPP
#define HEADER_HELPER_HPP

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool is_little_endian = true;
#else
constexpr bool is_little_endian = false;
#endif

inline uint64_t byteSwap64(uint64_t val) {
    return (static_cast<uint64_t>(htonl(val & 0xFFFFFFFF)) << 32) | htonl(val >> 32);
}

inline uint64_t htonll(uint64_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return byteSwap64(val);
#else
    return val;
#endif
}

inline uint64_t ntohll(uint64_t val) {
    return byteSwap64(val);
}

inline __uint128_t htondll(__uint128_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t hi = htonll(static_cast<uint64_t>(val >> 64));
    uint64_t lo = htonll(static_cast<uint64_t>(val & 0xFFFFFFFFFFFFFFFF));
    return (static_cast<__uint128_t>(htonll(lo)) << 64) | htonll(hi);
#else
    return val;
#endif
}

inline __uint128_t ntohdll(__uint128_t val) {
    return htondll(val);
}

#define DEFINE_PACKET_HEADER(RAWTYPE)                                                               \
    using RawType = RAWTYPE;                                                                        \
    inline static constexpr size_t fixedSize = sizeof(RawType);                                     \
    RawType* raw = nullptr;                                                                         \
    uint8_t* buffer = nullptr;                                                                      \
    std::span<uint8_t> trailing;                                                                    \
                                                                                                    \
    void setBuffer(uint8_t* buf)                                                                    \
    {                                                                                               \
        raw = reinterpret_cast<RAWTYPE*>(buf);                                                      \
        buffer = buf;                                                                               \
        trailing = std::span<uint8_t>(reinterpret_cast<uint8_t*>(raw) + fixedSize, 0);              \
    }                                                                                               \
                                                                                                    \
    bool parse(uint8_t* data, size_t len, size_t headerSize, size_t startIndex = 0)                 \
    {                                                                                               \
        if (!data || len < startIndex + headerSize || len < startIndex + fixedSize) return false;   \
        raw = reinterpret_cast<RAWTYPE*>(const_cast<uint8_t*>(data + startIndex));                  \
        size_t remaining = headerSize - fixedSize;                                                  \
        trailing = std::span<uint8_t>(data + startIndex + fixedSize, remaining);                    \
        return true;                                                                                \
    }                                                                                               \
                                                                                                    \
    uint16_t encapsulate(uint8_t* out, size_t startIndex, size_t maxSize) const                     \
    {                                                                                               \
        if (!raw || !out) return 0;                                                                 \
        size_t totalSize = fixedSize + trailing.size();                                             \
        if (startIndex + totalSize > maxSize) return 0;                                             \
        const uint8_t* base = reinterpret_cast<const uint8_t*>(raw);                                \
        std::memcpy(out + startIndex, base, fixedSize);                                             \
        if (!trailing.empty()) {                                                                    \
            std::memcpy(out + startIndex + fixedSize, trailing.data(), trailing.size());            \
        }                                                                                           \
        return totalSize;                                                                           \
    }                                                                                               \
                                                                                                    \
    std::span<uint8_t> getTrail() const { return trailing; }                                        \
    uint8_t* getTrailData() { return trailing.data(); }                                             \
    void setTrail(uint8_t* data, size_t len)                                                        \
        { trailing = std::span<uint8_t>(data, len); }


#define DEFINE_FIXED_HEADER(RAWTYPE)                                                                \
    static constexpr size_t fixedSize = sizeof(RAWTYPE);                                            \
    RAWTYPE* raw = nullptr;                                                                         \
                                                                                                    \
    void setBuffer(uint8_t* buffer)                                                                 \
    {                                                                                               \
        raw = reinterpret_cast<RAWTYPE*>(buffer);                                                   \
    }                                                                                               \
                                                                                                    \
    bool parse(const uint8_t* data, size_t len, size_t startIndex = 0)                              \
    {                                                                                               \
        if (!data || len < startIndex + fixedSize) return false;                                    \
        raw = reinterpret_cast<RAWTYPE*>(const_cast<uint8_t*>(data + startIndex));                  \
        return true;                                                                                \
    }                                                                                               \
                                                                                                    \
    uint16_t encapsulate(uint8_t* out, size_t startIndex, size_t maxSize) const                     \
    {                                                                                               \
        if (!raw || !out) return 0;                                                                 \
        if (startIndex + fixedSize > maxSize) return 0;                                             \
        const uint8_t* base = reinterpret_cast<const uint8_t*>(raw);                                \
        std::memcpy(out + startIndex, base, fixedSize);                                             \
        return fixedSize;                                                                           \
    }


inline static uint16_t readU16(const uint8_t* p)
{
    if constexpr (is_little_endian)
    {
        return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
    }
    else 
    {
        uint16_t val;
        std::memcpy(&val, p, sizeof(val));
        return val;
    }
}

inline static uint32_t readU24(const uint8_t* p)
{
    if constexpr (is_little_endian)
    {
        return (uint32_t(p[0]) << 16) |
               (uint32_t(p[1]) << 8)  |
               uint32_t(p[2]);
    }
    else
    {
        return (uint32_t(p[0]) << 0)  |
               (uint32_t(p[1]) << 8)  |
               (uint32_t(p[2]) << 16);
    }
}

inline static uint32_t readU32(const uint8_t* p)
{
    if constexpr (is_little_endian)
    {
        return (uint32_t(p[0]) << 24) |
               (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8)  |
               uint32_t(p[3]);
    }
    else
    {
        uint32_t val;
        std::memcpy(&val, p, sizeof(val));
        return val;
    }
}

inline static uint64_t readU48(const uint8_t* p) {
    if constexpr (is_little_endian) {
        return (uint64_t(p[0]) << 40) |
               (uint64_t(p[1]) << 32) |
               (uint64_t(p[2]) << 24) |
               (uint64_t(p[3]) << 16) |
               (uint64_t(p[4]) << 8)  |
               uint64_t(p[5]);
    } else {
        return (uint64_t(p[0]) << 0)  |
               (uint64_t(p[1]) << 8)  |
               (uint64_t(p[2]) << 16) |
               (uint64_t(p[3]) << 24) |
               (uint64_t(p[4]) << 32) |
               (uint64_t(p[5]) << 40);
    }
}

inline static uint64_t readU64(const uint8_t* p)
{
    if constexpr (is_little_endian)
    {
        return (uint64_t(p[0]) << 56) |
               (uint64_t(p[1]) << 48) |
               (uint64_t(p[2]) << 40) |
               (uint64_t(p[3]) << 32) |
               (uint64_t(p[4]) << 24) |
               (uint64_t(p[5]) << 16) |
               (uint64_t(p[6]) << 8)  |
               uint64_t(p[7]);
    }
    else
    {
        uint64_t val;
        std::memcpy(&val, p, sizeof(val));
        return val;
    }
}

inline static __uint128_t readU128(const uint8_t* p)
{
    if constexpr (is_little_endian)
    {
        __uint128_t result = 0;
        for (size_t i = 0; i < 16; ++i)
        {
            result = (result << 8) | p[i];
        }
        return result;
    }
    else
    {
        __uint128_t val;
        std::memcpy(&val, p, sizeof(val));
        return val;
    }
}

inline static uint8_t* writeU16(uint8_t* dest, uint16_t val)
{
    if constexpr (is_little_endian)
    {
        dest[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
        dest[1] = static_cast<uint8_t>(val & 0xFF);
    }
    else
    {
        std::memcpy(dest, &val, sizeof(val));
    }
    return dest;
}

inline static uint8_t* writeU24(uint8_t* dest, uint32_t val)
{
    if constexpr (is_little_endian)
    {
        dest[0] = static_cast<uint8_t>((val >> 16) & 0xFF);
        dest[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        dest[2] = static_cast<uint8_t>(val & 0xFF);
    }
    else
    {
        std::memcpy(dest, reinterpret_cast<const uint8_t*>(&val) + 1, 3);
    }
    return dest;
}

inline static uint8_t* writeU32(uint8_t* dest, uint32_t val)
{
    if constexpr (is_little_endian)
    {
        dest[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
        dest[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
        dest[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
        dest[3] = static_cast<uint8_t>(val & 0xFF);
    }
    else
    {
        std::memcpy(dest, &val, sizeof(val));
    }
    return dest;
}

inline static uint8_t* writeU48(uint8_t* dest, uint64_t val) {
    dest[0] = static_cast<uint8_t>((val >> 40) & 0xFF);
    dest[1] = static_cast<uint8_t>((val >> 32) & 0xFF);
    dest[2] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dest[3] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dest[4] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dest[5] = static_cast<uint8_t>(val & 0xFF);
    return dest;
}

inline static uint8_t* writeU64(uint8_t* dest, uint64_t val)
{
    if constexpr (is_little_endian)
    {
        dest[0] = static_cast<uint8_t>((val >> 56) & 0xFF);
        dest[1] = static_cast<uint8_t>((val >> 48) & 0xFF);
        dest[2] = static_cast<uint8_t>((val >> 40) & 0xFF);
        dest[3] = static_cast<uint8_t>((val >> 32) & 0xFF);
        dest[4] = static_cast<uint8_t>((val >> 24) & 0xFF);
        dest[5] = static_cast<uint8_t>((val >> 16) & 0xFF);
        dest[6] = static_cast<uint8_t>((val >> 8) & 0xFF);
        dest[7] = static_cast<uint8_t>(val & 0xFF);
    }
    else
    {
        std::memcpy(dest, &val, sizeof(val));
    }
    return dest;
}

inline static uint8_t* writeU128(uint8_t* dest, __uint128_t val)
{
    if constexpr (is_little_endian)
    {
        for (size_t i = 15; i >= 0; --i)
        {
            dest[i] = static_cast<uint8_t>(val & 0xFF);
            val >>= 8;
        }
    }
    else
    {
        std::memcpy(dest, &val, sizeof(val));
    }
    return dest;
}

inline static uint8_t* setBit(uint8_t* bytes, uint8_t bitIndex, bool value)
{
    size_t byteIndex = bitIndex / 8;
    uint8_t bitOffset = bitIndex % 8;
    bytes[byteIndex] ^= (1 << bitOffset);
    return bytes;
}

#endif
