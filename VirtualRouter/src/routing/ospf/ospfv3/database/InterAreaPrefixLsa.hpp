/**
 * @file InterAreaPrefixLsa.hpp
 * @brief OSPFv3 Inter-Area-Prefix LSA format.
 */

#ifndef INTER_AREA_PREFIX_LSA_HPP
#define INTER_AREA_PREFIX_LSA_HPP

#include <cstdint>
#include <IPAddress.h>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{

/**
 * @brief OSPFv3 Inter-Area-Prefix LSA body.
 * @ingroup OSPF_V3_DATABASE
 *
 * Advertises an IPv6 prefix from another area (ABR summary).
 * Carries:
 * - 24-bit metric to the destination
 * - Prefix options and IPv6 prefix
 *
 * Wire format requires:
 * - Reserved fields MUST be zero
 * - Prefix is padded to 32-bit word boundaries
 *
 * No optional fields; structure is fixed beyond prefix length.
 */
struct InterAreaPrefixLsa
{
    uint32_t metric;              ///< 24-bit inter-area metric.
    uint8_t options;              ///< Prefix options field.
    types::IPv6Prefix prefix;     ///< Advertised IPv6 prefix.

    /**
     * @brief Parse an Inter-Area-Prefix LSA body.
     *
     * Validates reserved fields and ensures prefix padding aligns
     * to 32-bit boundaries. Rejects any non-zero trailing bytes.
     *
     * @param buf Input buffer.
     * @param len Buffer length.
     * @return Parsed LSA or nullopt on failure.
     *
     * @warning Reserved fields must be zero or parsing fails.
     */
    static std::optional<InterAreaPrefixLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        InterAreaPrefixLsa lsa;

        if (buf[0] != 0) return std::nullopt;
        lsa.metric = utils::readU24(buf + 1);

        uint8_t prefixLen = buf[4];
        lsa.options = buf[5];
        if (utils::readU16(buf + 6) != 0) return std::nullopt;

        uint8_t prefixWords = (prefixLen + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;

        if (prefixBytes > static_cast<uint8_t>(16)) return std::nullopt;
        if (8 + prefixBytes > len) return std::nullopt;

        lsa.prefix = types::IPv6Prefix(buf + 8, prefixLen);

        for (size_t i = 8 + prefixBytes; i < len; ++i)
        {
            if (buf[i] != 0) return std::nullopt;
        }

        return lsa;
    }

    /**
     * @brief Serialize the LSA body into a buffer.
     *
     * Writes fixed fields and prefix padded to 32-bit alignment.
     *
     * @param[out] buf Output buffer.
     * @param len Buffer size.
     * @return True on success, false if buffer too small.
     *
     * @warning Padding beyond prefix length is not explicitly zeroed.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 8) return false;

        buf[0] = 0;
        utils::writeU24(buf + 1, metric);
        buf[4] = prefix.prefixLength;
        buf[5] = options;
        buf[6] = 0;
        buf[7] = 0;

        uint8_t prefixWords = (prefix.prefixLength + 31) / 32;
        uint8_t prefixBytes = prefixWords * 4;
        if (8 + prefixBytes > len) return false;
        utils::writeBytes(buf + 8, prefix.addr, prefixBytes);

        return true;
    }

    /**
     * @brief Compute serialized size of the LSA body.
     *
     * Includes fixed header and prefix padded to 32-bit words.
     *
     * @return Total size in bytes.
     */
    inline uint16_t size() const
    {
        uint8_t prefixWords = (prefix.prefixLength + 31) / 32;
        return 8 + (prefixWords * 4);
    }

    /**
     * @brief Append fields to Fletcher checksum.
     *
     * Adds metric, prefix metadata, and padded prefix bytes
     * in wire order.
     *
     * @param[in,out] check Checksum accumulator.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU24(metric);
        check.add(prefix.prefixLength);
        check.add(options);

        uint8_t prefixBytes = ((prefix.prefixLength + 31) / 32) * 4;
        for (size_t i = 0; i < 16 || i < prefixBytes; ++i)
        {
            check.add(prefix.raw()[i]);
        }
    }
};

} // namespace routing::ospf

#endif // INTER_AREA_PREFIX_LSA_HPP
