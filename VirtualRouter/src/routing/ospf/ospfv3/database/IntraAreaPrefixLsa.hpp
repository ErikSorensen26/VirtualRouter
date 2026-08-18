/**
 * @file IntraAreaPrefixLsa.hpp
 * @brief OSPFv3 Intra-Area-Prefix LSA body: IPv6 prefixes advertised within a single area.
 */

#ifndef INTRA_AREA_PREFIX_HPP
#define INTRA_AREA_PREFIX_HPP

#include <IPAddress.h>
#include <optional>
#include <vector>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief A single IPv6 prefix entry inside an Intra-Area-Prefix LSA.
 * @ingroup OSPF_V3_DATABASE
 *
 * Holds the prefix length, metric, and option flags (No-Unicast, Local
 * Address, Multicast, Propagate) for one prefix. A lightweight value type
 * owned by the containing IntraAreaPrefixLsa's prefix vector.
 */
struct IntraAreaPrefix
{
    uint8_t options;           ///< Option flags byte.
    uint16_t metric;           ///< Cost to reach this prefix.
    types::IPv6Prefix prefix;  ///< IPv6 prefix and length.

    void setNoUnicast(bool val) { utils::setBit(&options, 7, val); }    ///< Sets the NU (no-unicast) prefix option bit.
    void setLocalAddress(bool val) { utils::setBit(&options, 6, val); } ///< Sets the LA (local address) prefix option bit.
    void setMulticast(bool val) { utils::setBit(&options, 5, val); }    ///< Sets the MC (multicast) prefix option bit.
    void setPropagate(bool val) { utils::setBit(&options, 4, val); }    ///< Sets the P (propagate, NSSA) prefix option bit.

    bool operator==(const IntraAreaPrefix& rhs) const noexcept
    {
        return options == rhs.options &&
               metric == rhs.metric &&
               prefix == rhs.prefix;
    }
};

/**
 * @brief OSPFv3 Intra-Area-Prefix LSA body: IPv6 prefixes attached to a router,
 * transit network, or stub link within a single area.
 * @ingroup OSPF_V3_DATABASE
 *
 * Identifies the LSA (Router or Network) that these prefixes are attached to
 * via @ref referencedLsaType, @ref referencedLinkStateId, and
 * @ref referencedAdvRouter, followed by the owned vector of prefix entries.
 */
struct IntraAreaPrefixLsa
{
    uint16_t referencedLsaType;        ///< Type of referenced LSA.
    uint32_t referencedLinkStateId;    ///< Link State ID of referenced LSA.
    uint32_t referencedAdvRouter;      ///< Advertising router of referenced LSA.
    std::vector<IntraAreaPrefix> prefixes; ///< Contained prefixes.

    /**
     * @brief Parses a raw buffer into an IntraAreaPrefixLsa.
     *
     * Validates buffer length and prefix contents. Returns std::nullopt if
     * the buffer is truncated or malformed.
     *
     * @param buf Pointer to raw LSA buffer.
     * @param len Length of the buffer in bytes.
     * @return Optional IntraAreaPrefixLsa if parsing succeeds, nullopt if fails.
     */
    static std::optional<IntraAreaPrefixLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 12) return std::nullopt;

        uint16_t prefixCount = utils::read<uint16_t>(buf);
        IntraAreaPrefixLsa lsa;
        lsa.referencedLsaType = utils::read<uint16_t>(buf + 2);
        lsa.referencedLinkStateId = utils::read<uint32_t>(buf + 4);
        lsa.referencedAdvRouter = utils::read<uint32_t>(buf + 8);

        size_t off = 12;
        for (uint16_t i = 0; i < prefixCount; i++)
        {
            if (off + 4 > len) return std::nullopt;

            uint8_t plen = buf[off++];
            IntraAreaPrefix prefix;
            prefix.options = buf[off++];
            prefix.metric = utils::read<uint16_t>(buf + off);
            off += 2;

            uint8_t prefixBytes = (plen + 7) / 8;
            if (off + prefixBytes > len) return std::nullopt;
            prefix.prefix = types::IPv6Prefix(buf + off, plen);
            off += prefixBytes;

            lsa.prefixes.push_back(prefix);
        }

        return lsa;
    }

    /**
     * @brief Serializes the LSA into a wire-format buffer.
     *
     * Writes header fields and all contained prefixes. Validates buffer length.
     *
     * @param buf Buffer to write serialized data.
     * @param len Total buffer length in bytes.
     * @return True if serialization succeeds, false if buffer too small.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 12) return false;

        utils::write<uint16_t>(buf, static_cast<uint16_t>(prefixes.size()));
        utils::write<uint16_t>(buf + 2, referencedLsaType);
        utils::write<uint32_t>(buf + 4, referencedLinkStateId);
        utils::write<uint32_t>(buf + 8, referencedAdvRouter);

        size_t off = 12;
        for (const auto& prefix : prefixes)
        {
            if (off + 4 > len) return false;

            buf[off++] = prefix.prefix.prefixLength;
            buf[off++] = prefix.options;
            utils::write<uint16_t>(buf + off, prefix.metric);
            off += 2;

            uint8_t prefixBytes = (prefix.prefix.prefixLength + 7) / 8;
            if (off + prefixBytes > len) return false;
            utils::write<__uint128_t>(buf + off, prefix.prefix.addr, prefixBytes);
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

#endif // INTRA_AREA_PREFIX_HPP
