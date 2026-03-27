/**
 * @file OspfFletcher.hpp
 * @brief Incremental Fletcher-16 checksum accumulator for OSPF LSA bodies.
 */

#ifndef OSPF_FLETCHER_HPP
#define OSPF_FLETCHER_HPP

#include <cstdint>
#include <cstddef>
#include <NetworkSpan.hpp>

namespace routing::ospf
{
/**
 * @brief Incremental Fletcher-16 checksum accumulator used to compute OSPF LSA checksums.
 * @ingroup OSPF_TRANSMISSION
 *
 * OSPF LSAs are checksummed over their body using the Fletcher-16 algorithm (RFC 2328
 * §C.1). Each LSA body type calls `appendChecksum()` to feed its fields into an instance
 * of this class, then the dispatcher reads `c0`/`c1` to write the two-byte checksum into
 * the LSA header before transmission.
 *
 * ## Architectural Role
 * This is a pure arithmetic helper with no state beyond the running accumulators. It is
 * stack-allocated per LSA during origination and transmission; it is never shared across
 * threads.
 *
 * @see PacketDispatcherV2, PacketDispatcherV3
 */
class ChecksumFletcher
{
public:
    uint16_t c0 = 0; ///< Lower running sum (mod 255).
    uint16_t c1 = 0; ///< Higher running sum (mod 255); combined with c0 to form the checksum.

    /**
     * @brief Feeds a single byte into the checksum.
     *
     * @param b Byte to accumulate.
     */
    inline void add(uint8_t b)
    {
        c0 += b;
        if (c0 >= 255) c0 -= 255;
        c1 += c0;
        if (c1 >= 255) c1 -= 255;
    }

    /**
     * @brief Feeds a 32-bit big-endian word into the checksum byte by byte.
     *
     * @param b Value to accumulate (host byte order, fed MSB-first).
     */
    inline void addU32(uint32_t b)
    {
        add(static_cast<uint8_t>(b >> 24));
        add(static_cast<uint8_t>((b >> 16) & 0xFF));
        add(static_cast<uint8_t>((b >> 8) & 0xFF));
        add(static_cast<uint8_t>(b & 0xFF));
    }

    /**
     * @brief Feeds the low 24 bits of a word into the checksum MSB-first.
     *
     * Used for OSPFv3 fields encoded as 3-byte big-endian integers.
     *
     * @param b Value to accumulate; only the lowest 24 bits are used.
     */
    inline void addU24(uint32_t b)
    {
        add(static_cast<uint8_t>((b >> 16) & 0xFF));
        add(static_cast<uint8_t>((b >> 8) & 0xFF));
        add(static_cast<uint8_t>(b & 0xFF));
    }

    /**
     * @brief Feeds a 16-bit big-endian word into the checksum byte by byte.
     *
     * @param b Value to accumulate (host byte order, fed MSB-first).
     */
    inline void addU16(uint16_t b)
    {
        add(static_cast<uint8_t>((b >> 8) & 0xFF));
        add(static_cast<uint8_t>(b & 0xFF));
    }

    /**
     * @brief Feeds a raw byte array into the checksum.
     *
     * @param data Pointer to the first byte.
     * @param len  Number of bytes to accumulate.
     */
    inline void addBytes(const uint8_t* data, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            add(data[i]);
        }
    }

    /**
     * @brief Feeds bytes from a @ref types::NetworkSpan into the checksum.
     *
     * @tparam N  Underlying span element type.
     * @param data Span of network data.
     * @param len  Number of bytes to accumulate.
     */
    template <typename N>
    inline void addBytes(const types::NetworkSpan<N>& data, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            add(data[i]);
        }
    }

    /**
     * @brief Returns the completed Fletcher-16 checksum as a packed 16-bit value.
     *
     * The result is `(c1 << 8) | c0` and is written directly into the two checksum
     * bytes of the LSA header.
     *
     * @return Packed 16-bit checksum.
     */
    inline uint16_t finalize() const
    {
        return static_cast<uint16_t>((c1 << 8) | c0);
    }
};
} // namespace routing

#endif

