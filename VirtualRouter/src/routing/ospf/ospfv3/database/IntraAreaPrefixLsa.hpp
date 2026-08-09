/**
 * @file IntraAreaPrefixLsa.hpp
 * @brief OSPFv3 Intra-Area-Prefix LSA representation and wire-format handling.
 *
 * Defines IntraAreaPrefix and IntraAreaPrefixLsa structures used to model
 * OSPFv3 LSAs advertising IPv6 prefixes within a single area. Supports parsing
 * from buffers, serialization, size calculation, and RFC-compliant checksums.
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
 * @brief Represents a single IPv6 prefix entry in an Intra-Area-Prefix LSA.
 * @ingroup OSPF_V3_DATABASE
 *
 * Contains the prefix length, metric, and option flags. Flags encode:
 * No-Unicast, Local Address, Multicast, and Propagate behaviors.
 *
 * ## Architectural Role
 * Mirrors RFC-defined LSA prefix structure. Used by IntraAreaPrefixLsa to
 * build full LSA payloads.
 *
 * ## Lifecycle & Ownership
 * Lightweight value type. Fully owned by the LSA container; safe to copy.
 *
 * @warning Metric and option flags must comply with RFC constraints.
 */
struct IntraAreaPrefix
{
    uint8_t options;           ///< Option flags byte.
    uint16_t metric;           ///< Cost to reach this prefix.
    types::IPv6Prefix prefix;  ///< IPv6 prefix and length.

    /**
     * @brief Sets or clears the No-Unicast flag (bit 7).
     *
     * Controls whether the prefix is allowed for unicast routing.
     *
     * @param val True to set, false to clear.
     */
    void setNoUnicast(bool val) { utils::setBit(&options, 7, val); }

    /**
     * @brief Sets or clears the Local Address flag (bit 6).
     *
     * Indicates if the prefix is associated with a local interface.
     *
     * @param val True to set, false to clear.
     */
    void setLocalAddress(bool val) { utils::setBit(&options, 6, val); }

    /**
     * @brief Sets or clears the Multicast flag (bit 5).
     *
     * Marks the prefix for multicast eligibility.
     *
     * @param val True to set, false to clear.
     */
    void setMulticast(bool val) { utils::setBit(&options, 5, val); }

    /**
     * @brief Sets or clears the Propagate flag (bit 4).
     *
     * Determines whether the prefix should be propagated to other routers.
     *
     * @param val True to set, false to clear.
     */
    void setPropagate(bool val) { utils::setBit(&options, 4, val); }

    /**
     * @brief Compares two prefixes for equality.
     *
     * Checks options, metric, and prefix fields.
     *
     * @param rhs Prefix to compare.
     * @return True if identical, false otherwise.
     */
    bool operator==(const IntraAreaPrefix& rhs) const noexcept
    {
        return options == rhs.options &&
               metric == rhs.metric &&
               prefix == rhs.prefix;
    }
};

/**
 * @brief Represents an OSPFv3 Intra-Area-Prefix LSA containing multiple prefixes.
 * @ingroup OSPF_V3_DATABASE
 *
 * Contains referenced LSA type, Link State ID, advertising router, and a vector
 * of IntraAreaPrefix entries. Supports parsing, serialization, size calculation,
 * and RFC-compliant checksums.
 *
 * ## Architectural Role
 * Forms the complete payload of an Intra-Area-Prefix LSA for flooding and
 * database storage. Works with checksum routines to ensure protocol compliance.
 *
 * ## Lifecycle & Ownership
 * Constructed via `build()` or manually populated. Owns its vector of prefixes;
 * no external references retained.
 *
 * @warning Truncated buffers or malformed prefixes cause parsing to fail.
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

    /**
     * @brief Computes the total serialized size of the LSA.
     *
     * Includes 12-byte header plus size of all contained prefixes.
     *
     * @return Length in bytes needed for serialization.
     */
    inline uint16_t size() const
    {
        uint16_t len = 12;
        for (const auto& prefix : prefixes)
        {
            len += 4 + ((prefix.prefix.prefixLength + 7) / 8);
        }
        return len;
    }

    /**
     * @brief Appends LSA fields to a Fletcher checksum.
     *
     * Used to validate integrity before transmission or storage.
     *
     * @param check Checksum object to which fields are added.
     */
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
