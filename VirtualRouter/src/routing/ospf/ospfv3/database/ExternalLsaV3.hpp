/**
 * @file ExternalLsaV3.hpp
 * @brief OSPFv3 AS-External LSA format and parsing.
 */

#ifndef EXTERNAL_LSA_V3_HPP
#define EXTERNAL_LSA_V3_HPP

#include <IPAddress.h>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{

/**
 * @brief OSPFv3 AS-External LSA body.
 * @ingroup OSPF_V3_DATABASE
 *
 * Represents an externally redistributed route in OSPFv3. Carries:
 * - External metric (Type 1 or Type 2)
 * - Destination IPv6 prefix
 * - Optional forwarding address and route tag
 *
 * Acts as the wire-format boundary for external route advertisement.
 * Does not perform policy, redistribution, or LSDB management.
 *
 * Field presence is controlled by option bits and must match encoding.
 */
struct ExternalLsaV3
{
    uint32_t metric;                              ///< 24-bit external metric.
    uint8_t options;                              ///< Prefix options field.
    uint16_t referencedLsType;                    ///< Referenced LSA type (0 if unused).
    types::IPv6Prefix prefix;                     ///< Advertised IPv6 prefix.
    bool isType2;                                 ///< True if Type-2 metric.
    std::optional<types::IPv6Address> forwardingAddress; ///< Optional forwarding address.
    std::optional<uint32_t> routeTag;             ///< Optional external route tag.
    std::optional<uint32_t> referencedLsId;       ///< Optional referenced LSA ID.

    /**
     * @brief Parse an AS-External LSA body from a buffer.
     *
     * Decodes fixed fields, then conditionally parses optional fields
     * based on option flags. Prefix length is validated and converted
     * to byte length for extraction.
     *
     * @param buf Input buffer.
     * @param len Buffer length.
     * @return Parsed LSA or nullopt on failure.
     *
     * @warning Any size or flag inconsistency aborts parsing.
     */
    static std::optional<ExternalLsaV3> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        ExternalLsaV3 lsa;

        uint8_t exOpts = buf[0];
        lsa.isType2 = (exOpts & 0x04) != 0;
        lsa.metric = utils::readU24(buf + 1);

        uint8_t prefixLen = buf[4];
        lsa.options = buf[5];
        lsa.referencedLsType = utils::readU16(buf + 6);

        if (prefixLen > 128) return std::nullopt;

        uint8_t prefixBytes = (prefixLen + 7) / 8;
        uint16_t off = 8;

        if (off + prefixBytes > len) return std::nullopt;

        lsa.prefix = types::IPv6Prefix(buf + 8, prefixLen);
        lsa.prefix.prefixLength = prefixLen;

        off += prefixBytes;

        if (exOpts & 0x02)
        {
            if (off + 16 > len) return std::nullopt;
            lsa.forwardingAddress.emplace(buf + off);
            off += 16;
        }
        if (exOpts & 0x01)
        {
            if (off + 4 > len) return std::nullopt;
            lsa.routeTag = utils::readU32(buf + off);
            off += 4;
        }
        if (lsa.referencedLsType != 0)
        {
            if (off + 4 > len) return std::nullopt;
            lsa.referencedLsId = utils::readU32(buf + off);
            off += 4;
        }

        if (off != len) return std::nullopt;

        return lsa;
    }

    /**
     * @brief Serialize the AS-External LSA body into a buffer.
     *
     * Writes fields in wire order and sets option bits based on
     * optional field presence.
     *
     * @param[out] buf Output buffer.
     * @param len Buffer size.
     * @return True on success, false if buffer too small.
     *
     * @warning referencedLsId must be present if referencedLsType != 0.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 8) return false;

        uint8_t& exOpts = buf[0];
        if (isType2) exOpts |= 0x04;
        utils::writeU24(buf + 1, metric);

        buf[4] = prefix.prefixLength;
        buf[5] = options;
        utils::writeU16(buf + 6, referencedLsType);

        uint8_t prefixBytes = (prefix.prefixLength + 7) / 8;
        uint16_t off = 8;

        if (off + prefixBytes > len) return false;

        utils::writeBytes(buf + 8, prefix.addr, prefixBytes);

        off += prefixBytes;

        if (forwardingAddress.has_value())
        {
            exOpts |= 0x02;
            if (off + 16 > len) return false;
            utils::writeU128(buf + off, forwardingAddress.value().addr);
            off += 16;
        }
        if (routeTag.has_value())
        {
            exOpts |= 0x01;
            if (off + 4 > len) return false;
            utils::writeU32(buf + off, routeTag.value());
            off += 4;
        }
        if (referencedLsType != 0)
        {
            if (off + 4 > len || !referencedLsId.has_value()) return false;
            utils::writeU32(buf + off, referencedLsId.value());
        }

        return true;
    }

    /**
     * @brief Compute serialized size of the LSA body.
     *
     * Includes fixed header, prefix bytes, and any optional
     * forwarding address, route tag, or referenced LSA fields.
     *
     * @return Total size in bytes.
     */
    inline uint16_t size() const
    {
        size_t len = 8 + (prefix.prefixLength + 7) / 8;
        if (forwardingAddress.has_value())
            len += 16;
        if (routeTag.has_value())
            len += 4;
        if (referencedLsId.has_value())
            len += 4;
        return static_cast<uint16_t>(len);
    }

    /**
     * @brief Append fields to Fletcher checksum.
     *
     * Adds fields in wire order with option-controlled presence
     * to ensure checksum consistency with serialization.
     *
     * @param[in,out] check Checksum accumulator.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        uint8_t extOpts{0};
        if (isType2) extOpts |= 0x04;
        if (forwardingAddress.has_value()) extOpts |= 0x02;
        if (routeTag.has_value()) extOpts |= 0x01;

        check.addU24(metric);
        check.add(prefix.prefixLength);
        check.addU16(options);

        uint8_t prefixBytes = (prefix.prefixLength + 7) / 8;
        check.addBytes(prefix.raw(), prefixBytes);

        if (extOpts & 0x02)
            check.addBytes(forwardingAddress.value().raw(), 16);
        if (extOpts & 0x01)
            check.addU32(routeTag.value());
        if (referencedLsType != 0)
            check.addU32(referencedLsId.value());
    }
};

} // namespace routing::ospf

#endif // EXTERNAL_LSA_V3_HPP
