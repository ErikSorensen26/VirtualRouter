/**
 * @file ByteUtils.hpp
 * @brief Network-order byte manipulation: endian conversion, fixed-width reads and writes. */

#ifndef BYTE_UTILS_HPP
#define BYTE_UTILS_HPP

#include <cstdint>
#include <cstring>
#include <cassert>
#include <arpa/inet.h>
#include <AddressFamily.hpp>
#include <type_traits>

namespace utils
{

/**
 * @brief True on platforms where the native byte order is little-endian.
 *
 * Used as a compile-time constant to select the correct byte-swap path.
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool isLittleEndian = true;
#else
constexpr bool isLittleEndian = false;
#endif

/**
 * @brief Swaps the byte order of a 32-bit value unconditionally.
 *
 * Implemented using one htonl call to ensure correctness on all
 * platforms without relying on compiler builtins.
 *
 * @param val  Value to byte-swap.
 * @return     @p val with its bytes reversed.
 */
inline uint32_t byteSwap32(uint32_t val)
{
    return ((val << 24) & 0xFF000000u) | ((val << 8) & 0x00FF0000u)
        | ((val >> 8) & 0x0000FF00u) | ((val >> 24) & 0x000000FFu);
}

/**
 * @brief Swaps the byte order of a 64-bit value unconditionally.
 *
 * Implemented using two htonl() calls to ensure correctness on all
 * platforms without relying on compiler builtins.
 *
 * @param val  Value to byte-swap.
 * @return     @p val with its bytes reversed.
 */
inline uint64_t byteSwap64(uint64_t val)
{
    return (static_cast<uint64_t>(byteSwap32(static_cast<uint32_t>(val))) << 32)
        | byteSwap32(static_cast<uint32_t>(val >> 32));
}

/**
 * @brief Converts a 64-bit value from host byte order to network (big-endian) byte order.
 *
 * A no-op on big-endian hosts. On little-endian hosts this delegates to byteSwap64().
 *
 * @param val  Host-order value.
 * @return     Network-order representation of @p val.
 */
inline uint64_t htonll(uint64_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return byteSwap64(val);
#else
    return val;
#endif
}

/**
 * @brief Converts a 64-bit value from network byte order to host byte order.
 *
 * Symmetric with htonll(); always performs a byte swap because the
 * underlying byteSwap64() is its own inverse.
 *
 * @param val  Network-order value.
 * @return     Host-order representation of @p val.
 */
inline uint64_t ntohll(uint64_t val) {
    return byteSwap64(val);
}

/**
 * @brief Converts a 128-bit value from host byte order to network (big-endian) byte order.
 *
 * Used for IPv6 address fields in packet headers. A no-op on big-endian hosts.
 *
 * @param val  Host-order 128-bit value.
 * @return     Network-order representation of @p val.
 */
inline __uint128_t htondll(__uint128_t val) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t hi = htonll(static_cast<uint64_t>(val >> 64));
    uint64_t lo = htonll(static_cast<uint64_t>(val & 0xFFFFFFFFFFFFFFFF));
    return (static_cast<__uint128_t>(htonll(lo)) << 64) | htonll(hi);
#else
    return val;
#endif
}

/**
 * @brief Converts a 128-bit value from network byte order to host byte order.
 *
 * Symmetric with htondll(); delegates to htondll() because the operation is
 * self-inverse.
 *
 * @param val  Network-order 128-bit value.
 * @return     Host-order representation of @p val.
 */
inline __uint128_t ntohdll(__uint128_t val) {
    return htondll(val);
}

template <size_t b>
struct SmallestInteger
{
private:
    static constexpr size_t bytes = (b + 7) / 8;

    static_assert(b > 0, "Size must be at least 1 byte");
    static_assert(bytes <= 16, "No integer type available for this type");

public:
    using type =
        std::conditional_t<bytes <= 1, uint8_t,
        std::conditional_t<bytes <= 2, uint16_t,
        std::conditional_t<bytes <= 4, uint32_t,
        std::conditional_t<bytes <= 8, uint64_t,
        __uint128_t>>>>;
};

// MASK HELPERS

/**
 * TODO add doxy comment
 */
template <typename T>
inline T maskBits(size_t bits)
{
    constexpr size_t bitSiz = sizeof(T) * 8;
    assert(bits <= bitSiz);
    return (bits == bitSiz) ? T(0) : static_cast<T>((T(1) << bits) - 1);
}

// FIXED-WIDTH BIG-ENDIAN READS

/**
 * TODO add doxy comment
 */
template <typename T, size_t N = sizeof(T)>
requires std::is_integral_v<T>
inline T read(const uint8_t* p)
{
    static_assert(N >= 1 && N <= sizeof(T), "N must be in [1, sizeof(T)]");

    if constexpr (N == sizeof(T) && !isLittleEndian)
    {
        T val;
        std::memcpy(&val, p, sizeof(val));
        return val;
    }
    else
    {
        T val = 0;
        for (size_t i = 0; i < N; ++i)
        {
            val = static_cast<T>((val << 8) | T(p[i]));
        }
        return val;
    }
}

/**
 * TODO add doxy comment
 */
template <typename T>
requires std::is_integral_v<T>
inline T read(const uint8_t* p, size_t n)
{
    assert(n >= 1 && n <= sizeof(T));

    if (n == sizeof(T) && !isLittleEndian)
    {
        T val;
        std::memcpy(&val, p, sizeof(val));
        return val;
    }
    else
    {
        T val = 0;
        for (size_t i = 0; i < n; ++i)
        {
            val = static_cast<T>((val << 8) | T(p[i]));
        }
        return val;
    }
}

// FIXED-WIDTH BIG-ENDIAN WRITES

/**
 * TODO add doxy comment
 */
template <typename T, size_t N = sizeof(T)>
requires std::is_integral_v<T>
inline uint8_t* write(uint8_t* dest, T val)
{
    static_assert(N >= 1 && N <= sizeof(T), "N must be [1, sizeof(T)]");

    if constexpr (isLittleEndian)
    {
        for (int i = N - 1; i >= 0; --i)
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

/**
 * TODO add doxy comment
 */
template <typename T>
requires std::is_integral_v<T>
inline uint8_t* write(uint8_t* dest, T val, size_t n)
{
    assert(n >= 1 && n <= sizeof(T));

    if constexpr (isLittleEndian)
    {
        for (int i = n - 1; i >= 0; --i)
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

/**
 * @brief Sets or clears a single bit within a byte array using network bit ordering.
 *
 * Bit @p bitIndex 0 maps to the most-significant bit of byte 0, matching the
 * big-endian bit numbering convention used in protocol flag fields (e.g. OSPF
 * Options, BGP Capability flags).
 *
 * @param bytes     Pointer to the byte array to modify.
 * @param bitIndex  Zero-based bit index in network order (0 = MSB of byte 0).
 * @param value     True to set the bit, false to clear it.
 * @return          @p bytes (allows chaining).
 */
inline uint8_t* setBit(uint8_t* bytes, uint8_t bitIndex, bool value)
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
