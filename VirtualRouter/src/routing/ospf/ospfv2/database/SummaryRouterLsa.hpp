/**
 * @file SummaryRouterLsa.hpp
 * @brief OSPFv2 Summary (Type 4) LSA body: inter-area ASBR advertisement — RFC 2328 §A.4.4.
 */

#ifndef SUMMARY_ROUTER_LSA_HPP
#define SUMMARY_ROUTER_LSA_HPP

#include <cstdint>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief Wire-format body of an OSPFv2 Type-4 Summary LSA (inter-area ASBR reachability).
 * @ingroup OSPF_V2_DATABASE
 *
 * Originated by an ABR to advertise the cost to reach an ASBR located in
 * another area, as described in RFC 2328 §12.4.3 and §A.4.4.
 *
 * Fixed 8-byte body:
 * - 4 bytes: network mask field — always zero for Type-4 LSAs.
 * - 1 byte:  reserved (zero).
 * - 3 bytes: 24-bit metric.
 *
 * Unlike the Type-3 Summary LSA, this body carries no network mask; the ASBR
 * is identified solely by the LSA's Link-State ID (the ASBR's Router ID).
 *
 * Use @ref build to parse a received buffer, and @ref buildBody to serialise
 * for transmission.
 */
struct SummaryRouterLsa
{
    uint32_t metric;  ///< Cumulative cost to reach the ASBR (24 bits significant).

    /**
     * @brief Parses an OSPFv2 Summary Router LSA body from a wire buffer.
     * @param buf Pointer to the start of the LSA body (after the 20-byte LSA header).
     * @param len Length of @p buf in bytes; must be exactly 8.
     * @return Parsed struct on success, or @c std::nullopt if @p len is not 8
     *         or the reserved mask field is non-zero.
     */
    static std::optional<SummaryRouterLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 8) return std::nullopt;

        if (utils::readU32(buf) != 0) return std::nullopt;

        SummaryRouterLsa lsa;

        uint32_t metricWord = utils::readU32(buf + 4);
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

        utils::writeU32(buf, 0);
        utils::writeU32(buf + 4, metric);

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
        uint32_t metricWord = metric;
        metricWord |= 0x00FFFFFF;
        check.addU32(metricWord);
    }

    /**
     * @brief Compares two Summary Router LSAs for equality.
     * @param rhs The other LSA to compare against.
     * @return True if the metrics are identical.
     */
    bool operator==(const SummaryRouterLsa& rhs) const
    {
        return metric == rhs.metric;
    }
};
} // namespace routing::ospf

#endif // SUMMARY_ROUTER_LSA_HPP
