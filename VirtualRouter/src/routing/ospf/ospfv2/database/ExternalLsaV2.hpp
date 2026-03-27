/**
 * @file ExternalLsaV2.hpp
 * @brief OSPFv2 AS External (Type 5) and NSSA External (Type 7) LSA body — RFC 2328 §A.4.5.
 */

#ifndef EXTERNAL_LSA_V2_HPP
#define EXTERNAL_LSA_V2_HPP

#include <cstdint>
#include <IPAddress.h>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief Wire-format body of an OSPFv2 AS-External (Type 5) or NSSA-External (Type 7) LSA.
 * @ingroup OSPF_V2_DATABASE
 *
 * Fixed 16-byte structure as defined in RFC 2328 §A.4.5:
 * - Network mask of the advertised destination.
 * - Metric with an E-bit distinguishing Type-1 (E=0) from Type-2 (E=1) metrics.
 * - Optional forwarding address (non-zero overrides the ASBR as the next hop).
 * - External route tag (carried transparently by OSPF).
 *
 * Use @ref build to parse a received LSA body buffer, and @ref buildBody to
 * serialise for transmission.
 */
struct ExternalLsaV2
{
    uint32_t networkMask;        ///< Subnet mask of the advertised external destination.
    uint32_t metric;             ///< Route metric (24 bits used; high 8 bits reserved).
    bool isType2;                ///< True for Type-2 (E2) external metric; false for Type-1 (E1).
    uint32_t forwardingAddress;  ///< Forwarding address (0 = use ASBR as next hop).
    uint32_t routeTag;           ///< Opaque external route tag.

    /**
     * @brief Parses an OSPFv2 External LSA body from a wire buffer.
     * @param buf Pointer to the start of the LSA body (after the 20-byte LSA header).
     * @param len Length of @p buf in bytes; must be exactly 16.
     * @return Parsed struct on success, or @c std::nullopt if @p len is invalid.
     */
    static std::optional<ExternalLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 16) return std::nullopt;

        ExternalLsaV2 lsa;

        lsa.networkMask = utils::readU32(buf);

        uint32_t metricWord = utils::readU32(buf + 4);
        lsa.isType2 = (metricWord & 0x80000000) != 0;
        lsa.metric = metricWord & 0x7FFFFFFF;

        lsa.forwardingAddress = utils::readU32(buf + 8);
        lsa.routeTag = utils::readU32(buf + 12);

        return lsa;
    }

    /**
     * @brief Serialises this LSA body into a wire buffer.
     * @param buf Destination buffer; must be at least 16 bytes.
     * @param len Available bytes in @p buf; must be exactly 16.
     * @return True on success; false if @p len is not 16.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 16) return false;

        utils::writeU32(buf, networkMask);
        uint32_t metricWord = metric & 0x7FFFFFFF;
        if (isType2) metricWord |= 0x80000000;
        utils::writeU32(buf + 4, metricWord);
        if (isType2) buf[4] = 0x80;
        utils::writeU32(buf + 8, forwardingAddress);
        utils::writeU32(buf + 12, routeTag);
        return true;
    }

    /**
     * @brief Returns the fixed serialised size of this LSA body in bytes.
     * @return Always 16.
     */
    static constexpr uint16_t size()
    {
        return 16;
    }

    /**
     * @brief Feeds this LSA body's fields into a Fletcher checksum accumulator.
     * @param check Checksum accumulator to update.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(networkMask);
        uint32_t metricWord = metric & 0x7FFFFFFF;
        if (isType2) metricWord |= 0x80000000;
        check.addU32(metricWord);
        check.addU32(forwardingAddress);
        check.addU32(routeTag);
    }
};
} // namespace routing::ospf

#endif
