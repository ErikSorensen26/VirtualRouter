// ByteUtils.hpp

#ifndef BYTE_UTILS_HPP
#define BYTE_UTILS_HPP

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <AddressFamily.hpp>
#include <type_traits>

namespace utils
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool isLittleEndian = true;
#else
constexpr bool isLittleEndian = false;
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

inline static uint8_t maskU8Bits(unsigned bits)
{
    return (bits == 8) ? uint8_t(0) : static_cast<uint8_t>((uint8_t(1) << bits) - 1);
}

inline static uint16_t maskU16Bits(unsigned bits)
{
    return (bits == 16) ? uint16_t(0) : static_cast<uint16_t>((uint16_t(0) << bits) - 1);
}

inline static uint32_t maskU32Bits(unsigned bits)
{
    return (bits == 32) ? uint32_t(0) : static_cast<uint32_t>((uint32_t(0) << bits) - 1);
}

inline static uint64_t maskU64Bits(unsigned bits)
{
    return (bits == 64) ? uint64_t(0) : static_cast<uint64_t>((uint64_t(0) << bits) - 1);
}

inline static __uint128_t maskU128Bits(unsigned bits)
{
    return (bits == 128) ? __uint128_t(0) : ((__uint128_t(0) << bits) - 1);
}

inline static uint16_t readU16(const uint8_t* p)
{
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian) {
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
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
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

template <typename T>
inline static T readBytes(const uint8_t* p, size_t n)
{
    static_assert(std::is_unsigned_v<T>, "T must be unsigned");

    T val = 0;
    
    if constexpr (isLittleEndian)
    {
        size_t shift = (sizeof(T) - 1) * 8;

        for (size_t i = 0; i < n; ++i)
        {
            val |= (T(p[i]) << shift);
            shift -= 8;
        }
    }
    else
    {
        std::memcpy(&val, p, n);
    }
    return val;
}

inline static uint8_t* writeU16(uint8_t* dest, uint16_t val)
{
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
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
    if constexpr (isLittleEndian)
    {
        for (int i = 15; i >= 0; --i)
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

template <typename T>
inline static uint8_t* writeBytes(uint8_t* dest, T val, size_t n)
{
    static_assert(std::is_unsigned_v<T>, "T must be unsigned");

    if constexpr (isLittleEndian)
    {
        size_t shift = (sizeof(T) - 1) * 8;
        for (size_t i = 0; i < n; ++i)
        {
            dest[i] = static_cast<uint8_t>((val >> shift) & 0xFF);
            shift -= 8;
        }
    }
    else
    {
        std::memcpy(dest, reinterpret_cast<const uint8_t*>(&val), n);
    }
    return dest;
}

inline static uint8_t* setBit(uint8_t* bytes, uint8_t bitIndex, bool value)
{
    const size_t byteIndex = bitIndex / 8;
    const size_t bitInByte = 7 - (bitIndex % 8);
    const uint8_t mask = uint8_t(1u << bitInByte);

    if (value)
        bytes[byteIndex] |= mask;             // set to 1
    else
        bytes[byteIndex] &= ~mask;            // set to 0

    return bytes;
}
}

#endif // BYTE_UTILS_HPP