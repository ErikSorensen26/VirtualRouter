/**
 * @file ByteUtils.hpp
 * @brief Network-order byte manipulation: endian conversion, fixed-width reads and writes. */

#ifndef BYTE_UTILS_HPP
#define BYTE_UTILS_HPP

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <AddressFamily.hpp>
#include <type_traits>

namespace utils
{

/// True on platforms where the native byte order is little-endian.
/// Used as a compile-time constant to select the correct byte-swap path.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool isLittleEndian = true;
#else
constexpr bool isLittleEndian = false;
#endif

/**
 * @brief Swaps the byte order of a 64-bit value unconditionally.
 *
 * Implemented using two htonl() calls to ensure correctness on all
 * platforms without relying on compiler builtins.
 *
 * @param val  Value to byte-swap.
 * @return     @p val with its bytes reversed.
 */
inline uint64_t byteSwap64(uint64_t val) {
    return (static_cast<uint64_t>(htonl(val & 0xFFFFFFFF)) << 32) | htonl(val >> 32);
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
 * @brief Returns a mask with the low @p bits of a uint8_t cleared (host order).
 *
 * @param bits  Number of low bits to mask out (0–8). Passing 8 returns 0.
 * @return      Bitmask with the top (8 - @p bits) bits set.
 */
inline static uint8_t maskU8Bits(unsigned bits)
{
    return (bits == 8) ? uint8_t(0) : static_cast<uint8_t>((uint8_t(1) << bits) - 1);
}

/**
 * @brief Returns a mask with the low @p bits of a uint16_t cleared (host order).
 *
 * @param bits  Number of low bits to mask out (0–16). Passing 16 returns 0.
 * @return      Bitmask with the top (16 - @p bits) bits set.
 */
inline static uint16_t maskU16Bits(unsigned bits)
{
    return (bits == 16) ? uint16_t(0) : static_cast<uint16_t>((uint16_t(0) << bits) - 1);
}

/**
 * @brief Returns a mask with the low @p bits of a uint32_t cleared (host order).
 *
 * @param bits  Number of low bits to mask out (0–32). Passing 32 returns 0.
 * @return      Bitmask with the top (32 - @p bits) bits set.
 */
inline static uint32_t maskU32Bits(unsigned bits)
{
    return (bits == 32) ? uint32_t(0) : static_cast<uint32_t>((uint32_t(0) << bits) - 1);
}

/**
 * @brief Returns a mask with the low @p bits of a uint64_t cleared (host order).
 *
 * @param bits  Number of low bits to mask out (0–64). Passing 64 returns 0.
 * @return      Bitmask with the top (64 - @p bits) bits set.
 */
inline static uint64_t maskU64Bits(unsigned bits)
{
    return (bits == 64) ? uint64_t(0) : static_cast<uint64_t>((uint64_t(0) << bits) - 1);
}

/**
 * @brief Returns a mask with the low @p bits of a __uint128_t cleared (host order).
 *
 * @param bits  Number of low bits to mask out (0–128). Passing 128 returns 0.
 * @return      Bitmask with the top (128 - @p bits) bits set.
 */
inline static __uint128_t maskU128Bits(unsigned bits)
{
    return (bits == 128) ? __uint128_t(0) : ((__uint128_t(0) << bits) - 1);
}

// FIXED-WIDTH BIG-ENDIAN READS

/**
 * @brief Reads two bytes from @p p and returns them as a big-endian uint16_t.
 *
 * @param p  Pointer to at least 2 bytes of data in network byte order.
 * @return   Host-order uint16_t.
 */
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

/**
 * @brief Reads three bytes from @p p and returns them as a big-endian uint32_t.
 *
 * The most-significant byte of the returned uint32_t is always zero.
 *
 * @param p  Pointer to at least 3 bytes of data in network byte order.
 * @return   Host-order uint32_t with the high byte zeroed.
 */
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

/**
 * @brief Reads four bytes from @p p and returns them as a big-endian uint32_t.
 *
 * @param p  Pointer to at least 4 bytes of data in network byte order.
 * @return   Host-order uint32_t.
 */
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

/**
 * @brief Reads six bytes from @p p and returns them as a big-endian uint64_t.
 *
 * Used for 48-bit fields such as MAC addresses and MPLS labels.
 * The two most-significant bytes of the returned uint64_t are always zero.
 *
 * @param p  Pointer to at least 6 bytes of data in network byte order.
 * @return   Host-order uint64_t with the two high bytes zeroed.
 */
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

/**
 * @brief Reads eight bytes from @p p and returns them as a big-endian uint64_t.
 *
 * @param p  Pointer to at least 8 bytes of data in network byte order.
 * @return   Host-order uint64_t.
 */
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

/**
 * @brief Reads sixteen bytes from @p p and returns them as a big-endian __uint128_t.
 *
 * Used for IPv6 addresses stored in packet headers.
 *
 * @param p  Pointer to at least 16 bytes of data in network byte order.
 * @return   Host-order __uint128_t.
 */
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

/**
 * @brief Reads @p n bytes from @p p into the high bytes of an unsigned integer of type @p T.
 *
 * Partial-width reads used for variable-length protocol fields (e.g. BGP
 * prefix lengths where only a prefix-length/8 bytes are transmitted).
 * Bytes are placed in the most-significant positions of @p T; unused low
 * bytes are zero.
 *
 * @tparam T  Unsigned destination type. Must satisfy `std::is_unsigned_v<T>`.
 * @param p   Pointer to at least @p n bytes of data in network byte order.
 * @param n   Number of bytes to read (must be ≤ sizeof(T)).
 * @return    Host-order value of type @p T with the @p n bytes in the high positions.
 */
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

// FIXED-WIDTH BIG-ENDIAN WRITES

/**
 * @brief Writes @p val to @p dest in network (big-endian) byte order as two bytes.
 *
 * @param dest  Destination buffer; must have room for at least 2 bytes.
 * @param val   Host-order value to encode.
 * @return      @p dest (allows chaining).
 */
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

/**
 * @brief Writes the low 24 bits of @p val to @p dest in network byte order.
 *
 * @param dest  Destination buffer; must have room for at least 3 bytes.
 * @param val   Host-order value to encode; the high byte is ignored.
 * @return      @p dest (allows chaining).
 */
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

/**
 * @brief Writes @p val to @p dest in network (big-endian) byte order as four bytes.
 *
 * @param dest  Destination buffer; must have room for at least 4 bytes.
 * @param val   Host-order value to encode.
 * @return      @p dest (allows chaining).
 */
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

/**
 * @brief Writes the low 48 bits of @p val to @p dest in network byte order.
 *
 * Used for MAC addresses and other 6-byte protocol fields.
 *
 * @param dest  Destination buffer; must have room for at least 6 bytes.
 * @param val   Host-order value to encode; the two high bytes are ignored.
 * @return      @p dest (allows chaining).
 */
inline static uint8_t* writeU48(uint8_t* dest, uint64_t val) {
    dest[0] = static_cast<uint8_t>((val >> 40) & 0xFF);
    dest[1] = static_cast<uint8_t>((val >> 32) & 0xFF);
    dest[2] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dest[3] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dest[4] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dest[5] = static_cast<uint8_t>(val & 0xFF);
    return dest;
}

/**
 * @brief Writes @p val to @p dest in network (big-endian) byte order as eight bytes.
 *
 * @param dest  Destination buffer; must have room for at least 8 bytes.
 * @param val   Host-order value to encode.
 * @return      @p dest (allows chaining).
 */
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

/**
 * @brief Writes @p val to @p dest in network (big-endian) byte order as sixteen bytes.
 *
 * Used for IPv6 addresses.
 *
 * @param dest  Destination buffer; must have room for at least 16 bytes.
 * @param val   Host-order 128-bit value to encode.
 * @return      @p dest (allows chaining).
 */
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

/**
 * @brief Writes the @p n most-significant bytes of @p val to @p dest in network byte order.
 *
 * Partial-width writes mirror readBytes() and are used for variable-length
 * protocol fields where only the significant bytes of a prefix are encoded.
 *
 * @tparam T   Unsigned source type. Must satisfy `std::is_unsigned_v<T>`.
 * @param dest Destination buffer; must have room for at least @p n bytes.
 * @param val  Host-order value whose high @p n bytes are written.
 * @param n    Number of bytes to write (must be ≤ sizeof(T)).
 * @return     @p dest (allows chaining).
 */
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
