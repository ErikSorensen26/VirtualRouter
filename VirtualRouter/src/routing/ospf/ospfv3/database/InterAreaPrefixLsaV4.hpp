/**
 * @file InterAreaPrefixLsaV4.hpp
 * @brief OSPFv3 Inter-Area-Prefix LSA format for the IPv4 address family (RFC 5838).
 *
 * Mirrors InterAreaPrefixLsa.hpp with types::IPv4Prefix in place of
 * types::IPv6Prefix. Prefix padding is still to 32-bit word boundaries, but
 * an IPv4 prefix never exceeds 4 bytes (one word) versus IPv6's up to 16.
 */

#ifndef INTER_AREA_PREFIX_LSA_V4_HPP
#define INTER_AREA_PREFIX_LSA_V4_HPP

#include <cstdint>
#include <IPAddress.h>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{

/**
 * @brief OSPFv3 Inter-Area-Prefix LSA body carrying an IPv4 prefix (RFC 5838).
 * @ingroup OSPF_V3_DATABASE
 */
struct InterAreaPrefixLsaV4
{
    uint32_t metric;              ///< 24-bit inter-area metric.
    uint8_t options;              ///< Prefix options field.
    types::IPv4Prefix prefix;     ///< Advertised IPv4 prefix.

    /**
     * @brief Parses an Inter-Area-Prefix LSA body carrying an IPv4 prefix.
     * @param buf Pointer to the start of the LSA body.
     * @param len Available bytes in @p buf.
     * @return Decoded LSA, or std::nullopt if malformed.
     */
    static std::optional<InterAreaPrefixLsaV4> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        InterAreaPrefixLsaV4 lsa;

        if (buf[0] != 0) return std::nullopt;
        lsa.metric = utils::read<uint32_t, 3>(buf + 1);

        uint8_t prefixLen = buf[4];
        lsa.options = buf[5];
        if (utils::read<uint16_t>(buf + 6) != 0) return std::nullopt;

        uint8_t prefixWords = (prefixLen + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;

        if (prefixBytes > static_cast<uint8_t>(4)) return std::nullopt;
        if (8 + prefixBytes > len) return std::nullopt;

        lsa.prefix = types::IPv4Prefix(buf + 8, prefixLen);

        for (size_t i = 8 + prefixBytes; i < len; ++i)
        {
            if (buf[i] != 0) return std::nullopt;
        }

        return lsa;
    }

    /**
     * @brief Serialises this LSA body into a wire buffer.
     * @param buf Destination buffer; must be at least @ref size() bytes.
     * @param len Available bytes in @p buf.
     * @return True on success; false if @p len is too small.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 8) return false;

        buf[0] = 0;
        utils::write<uint32_t, 3>(buf + 1, metric);
        buf[4] = prefix.prefixLength;
        buf[5] = options;
        buf[6] = 0;
        buf[7] = 0;

        uint8_t prefixWords = (prefix.prefixLength + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;
        if (8 + prefixBytes > len) return false;
        utils::write<__uint128_t>(buf + 8, prefix.addr, prefixBytes);

        return true;
    }

    /// Serialised size in bytes: fixed 8-byte header plus the prefix, padded to a 32-bit word.
    inline uint16_t size() const
    {
        uint8_t prefixWords = (prefix.prefixLength + 31) / 32;
        return 8 + (prefixWords * 4);
    }

    /// Folds this LSA body's fields into @p check for OSPF LSA checksum computation.
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU24(metric);
        check.add(prefix.prefixLength);
        check.add(options);

        uint8_t prefixBytes = ((prefix.prefixLength + 31) / 32) * 4;
        for (size_t i = 0; i < 4 || i < prefixBytes; ++i)
        {
            check.add(prefix.raw()[i]);
        }
    }
};

} // namespace routing::ospf

#endif // INTER_AREA_PREFIX_LSA_V4_HPP
