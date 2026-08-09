/**
 * @file SummaryNetworkLsa.hpp
 * @brief OSPFv2 Summary (Type 3) LSA body: inter-area prefix advertisement — RFC 2328 §A.4.4.
 */

#ifndef SUMMARY_NETWORK_LSA_HPP
#define SUMMARY_NETWORK_LSA_HPP

#include <cstdint>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief Wire-format body of an OSPFv2 Type-3 Summary LSA (inter-area network prefix).
 * @ingroup OSPF_V2_DATABASE
 *
 * Originated by an ABR to advertise a network reachable in one area into all
 * other areas of the same process, as described in RFC 2328 §12.4.3 and §A.4.4.
 *
 * Fixed 8-byte body:
 * - 4 bytes: network mask of the destination prefix.
 * - 1 byte:  reserved (zero).
 * - 3 bytes: 24-bit metric.
 *
 * Use @ref build to parse a received buffer, and @ref buildBody to serialise
 * for transmission.
 */
struct SummaryNetworkLsa
{
    uint32_t networkMask;  ///< Subnet mask of the advertised inter-area destination.
    uint32_t metric;       ///< Cumulative cost to the destination (24 bits significant).

    /**
     * @brief Parses an OSPFv2 Summary Network LSA body from a wire buffer.
     * @param buf Pointer to the start of the LSA body (after the 20-byte LSA header).
     * @param len Length of @p buf in bytes; must be exactly 8.
     * @return Parsed struct on success, or @c std::nullopt if @p len is not 8.
     */
    static std::optional<SummaryNetworkLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 8) return std::nullopt;

        SummaryNetworkLsa lsa;
        lsa.networkMask = utils::read<uint32_t>(buf);
        uint32_t metricWord = utils::read<uint32_t>(buf + 4);
        lsa.metric = metricWord & 0x00FFFFFF;

        return lsa;
    }

    /**
     * @brief Serialises this LSA body into a wire buffer.
     * @param buf Destination buffer; must be at least 8 bytes.
     * @param len Available bytes in @p buf; must be exactly 8.
     * @return True on success; false if @p len is not 8.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 8) return false;

        utils::write<uint32_t>(buf, networkMask);
        utils::write<uint32_t>(buf + 4, metric);
        return true;
    }

    /**
     * @brief Returns the fixed serialised size of this LSA body in bytes.
     * @return Always 8.
     */
    static constexpr uint16_t size()
    {
        return 8;
    }

    /**
     * @brief Feeds this LSA body's fields into a Fletcher checksum accumulator.
     * @param check Checksum accumulator to update.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(networkMask);
        check.addU32(metric);
    }

    /**
     * @brief Compares two Summary Network LSAs for equality.
     * @param rhs The other LSA to compare against.
     * @return True if both network mask and metric are identical.
     */
    bool operator==(const SummaryNetworkLsa& rhs) const
    {
        return networkMask == rhs.networkMask && metric == rhs.metric;
    }
};
} // namespace routing::ospf

#endif // SUMMARY_NETWORK_LSA_HPP
