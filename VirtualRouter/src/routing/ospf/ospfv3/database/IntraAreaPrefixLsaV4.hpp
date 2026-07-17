/**
 * @file IntraAreaPrefixLsaV4.hpp
 * @brief OSPFv3 Intra-Area-Prefix LSA representation for the IPv4 address family (RFC 5838).
 *
 * Mirrors IntraAreaPrefixLsa.hpp with types::IPv4Prefix in place of
 * types::IPv6Prefix. Wire format (TLV layout, per-prefix header) is identical
 * between address families per RFC 5838 -- only the prefix encoding differs.
 */

#ifndef INTRA_AREA_PREFIX_V4_HPP
#define INTRA_AREA_PREFIX_V4_HPP

#include <IPAddress.h>
#include <optional>
#include <vector>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief Represents a single IPv4 prefix entry in an Intra-Area-Prefix LSA (RFC 5838).
 * @ingroup OSPF_V3_DATABASE
 */
struct IntraAreaPrefixV4
{
    uint8_t options;           ///< Option flags byte.
    uint16_t metric;           ///< Cost to reach this prefix.
    types::IPv4Prefix prefix;  ///< IPv4 prefix and length.

    void setNoUnicast(bool val) { utils::setBit(&options, 7, val); }    ///< Sets the NU (no-unicast) prefix option bit.
    void setLocalAddress(bool val) { utils::setBit(&options, 6, val); } ///< Sets the LA (local address) prefix option bit.
    void setMulticast(bool val) { utils::setBit(&options, 5, val); }    ///< Sets the MC (multicast) prefix option bit.
    void setPropagate(bool val) { utils::setBit(&options, 4, val); }    ///< Sets the P (propagate, NSSA) prefix option bit.

    bool operator==(const IntraAreaPrefixV4& rhs) const noexcept
    {
        return options == rhs.options &&
               metric == rhs.metric &&
               prefix == rhs.prefix;
    }
};

/**
 * @brief Represents an OSPFv3 Intra-Area-Prefix LSA carrying IPv4 prefixes (RFC 5838).
 * @ingroup OSPF_V3_DATABASE
 */
struct IntraAreaPrefixLsaV4
{
    uint16_t referencedLsaType;        ///< Type of referenced LSA.
    uint32_t referencedLinkStateId;    ///< Link State ID of referenced LSA.
    uint32_t referencedAdvRouter;      ///< Advertising router of referenced LSA.
    std::vector<IntraAreaPrefixV4> prefixes; ///< Contained prefixes.

    /**
     * @brief Parses an Intra-Area-Prefix LSA body carrying IPv4 prefixes.
     * @param buf Pointer to the start of the LSA body.
     * @param len Available bytes in @p buf.
     * @return Decoded LSA, or std::nullopt if malformed.
     */
    static std::optional<IntraAreaPrefixLsaV4> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 12) return std::nullopt;

        uint16_t prefixCount = utils::readU16(buf);
        IntraAreaPrefixLsaV4 lsa;
        lsa.referencedLsaType = utils::readU16(buf + 2);
        lsa.referencedLinkStateId = utils::readU32(buf + 4);
        lsa.referencedAdvRouter = utils::readU32(buf + 8);

        size_t off = 12;
        for (uint16_t i = 0; i < prefixCount; i++)
        {
            if (off + 4 > len) return std::nullopt;

            uint8_t plen = buf[off++];
            IntraAreaPrefixV4 prefix;
            prefix.options = buf[off++];
            prefix.metric = utils::readU16(buf + off);
            off += 2;

            uint8_t prefixBytes = (plen + 7) / 8;
            if (off + prefixBytes > len) return std::nullopt;
            prefix.prefix = types::IPv4Prefix(buf + off, plen);
            off += prefixBytes;

            lsa.prefixes.push_back(prefix);
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
        if (len < 12) return false;

        utils::writeU16(buf, static_cast<uint16_t>(prefixes.size()));
        utils::writeU16(buf + 2, referencedLsaType);
        utils::writeU32(buf + 4, referencedLinkStateId);
        utils::writeU32(buf + 8, referencedAdvRouter);

        size_t off = 12;
        for (const auto& prefix : prefixes)
        {
            if (off + 4 > len) return false;

            buf[off++] = prefix.prefix.prefixLength;
            buf[off++] = prefix.options;
            utils::writeU16(buf + off, prefix.metric);
            off += 2;

            uint8_t prefixBytes = (prefix.prefix.prefixLength + 7) / 8;
            if (off + prefixBytes > len) return false;
            utils::writeBytes(buf + off, prefix.prefix.addr, prefixBytes);
            off += prefixBytes;
        }

        return true;
    }

    /// Serialised size in bytes: fixed 12-byte header plus each prefix's 4-byte header and prefix bytes.
    inline uint16_t size() const
    {
        uint16_t len = 12;
        for (const auto& prefix : prefixes)
        {
            len += 4 + ((prefix.prefix.prefixLength + 7) / 8);
        }
        return len;
    }

    /// Folds this LSA body's fields into @p check for OSPF LSA checksum computation.
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU16(static_cast<uint16_t>(prefixes.size()));
        check.addU16(referencedLsaType);
        check.addU32(referencedLinkStateId);
        check.addU32(referencedAdvRouter);

        for (const auto& prefix : prefixes)
        {
            check.add(prefix.prefix.prefixLength);
            check.add(prefix.options);
            check.addU16(prefix.metric);

            uint8_t prefixBytes = (prefix.prefix.prefixLength + 7) / 8;
            check.addBytes(prefix.prefix.raw(), prefixBytes);
        }
    }
};
} // namespace routing::ospf

#endif // INTRA_AREA_PREFIX_V4_HPP
