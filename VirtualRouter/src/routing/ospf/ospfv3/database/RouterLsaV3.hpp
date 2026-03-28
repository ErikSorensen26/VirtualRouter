/**
 * @file RouterLsaV3.hpp
 * @brief OSPFv3 Router LSA representation and wire-format handling.
 *
 * Defines RouterLinkV3 and RouterLsaV3 structures. Supports parsing from
 * wire-format buffers, serialization, size calculation, checksum computation,
 * and equality comparison. Each RouterLinkV3 represents a link advertised by
 * the router.
 */

/**
 * @defgroup OSPF_V3_DATABASE OSPFv3 Database
 * @ingroup OSPF_V3
 * @brief OSPFv3 LSA type definitions: Router, Network, External, Inter-Area, Intra-Area, Link.
 */

#ifndef ROUTER_LSA_V3_HPP
#define ROUTER_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <algorithm>
#include <numeric>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{


/**
 * @brief Represents a single link in a Router LSA.
 * @ingroup OSPF_V3_DATABASE
 *
 * Each RouterLinkV3 contains the type, metric, local interface ID, neighbor
 * interface ID, and neighbor router ID.
 *
 * ## Architectural Role
 * Used as a building block by RouterLsaV3 to describe all router-attached links.
 *
 * @warning All IDs must conform to OSPFv3 identifier constraints.
 */
struct RouterLinkV3
{
    uint8_t type;                 ///< Link type (point-to-point, transit, stub, etc.)
    uint16_t metric;              ///< Cost of traversing this link.
    uint32_t interfaceId;         ///< Local interface ID.
    uint32_t neighborInterfaceId; ///< Neighbor interface ID.
    uint32_t neighborRouterId;    ///< Neighbor router ID.

    /**
     * @brief Compares two links for equality.
     *
     * All fields must match exactly.
     *
     * @param rhs Link to compare against.
     * @return True if all fields are equal.
     */
    bool operator==(const RouterLinkV3& rhs) const noexcept
    {
        return type == rhs.type &&
               metric == rhs.metric &&
               interfaceId == rhs.interfaceId &&
               neighborInterfaceId == rhs.neighborInterfaceId &&
               neighborRouterId == rhs.neighborRouterId;
    }

    /**
     * @brief Less-than operator for sorting links.
     *
     * Compares type, interfaceId, neighborInterfaceId, and neighborRouterId.
     *
     * @param rhs Link to compare.
     * @return True if this link is less than rhs.
     */
    bool operator<(const RouterLinkV3& rhs) const noexcept
    {
        return std::tie(type, interfaceId, neighborInterfaceId, neighborRouterId)
             < std::tie(rhs.type, rhs.interfaceId, rhs.neighborInterfaceId, rhs.neighborRouterId);
    }
};


/**
 * @brief Represents an OSPFv3 Router LSA.
 * @ingroup OSPF_V3_DATABASE
 *
 * Stores router options and a list of advertised links. Supports parsing,
 * serialization, size calculation, checksum computation, and equality checks.
 *
 * ## Lifecycle & Ownership
 * Constructed via `build()` or manually. Owns its links vector; safe to copy.
 *
 * @warning Buffer lengths must match 16-byte link entries during parsing.
 */
struct RouterLsaV3
{
    uint32_t options;                 ///< Router options field.
    std::vector<RouterLinkV3> links; ///< Vector of links advertised by the router.

    /**
     * @brief Parses a raw buffer into a RouterLsaV3.
     *
     * Validates header and link entries. Returns std::nullopt if buffer is
     * truncated or malformed.
     *
     * @param buf Pointer to raw buffer.
     * @param len Length of the buffer in bytes.
     * @return Optional RouterLsaV3 if parsing succeeds, nullopt on failure.
     */
    static std::optional<RouterLsaV3> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        RouterLsaV3 lsa;

        lsa.options = utils::readU32(buf);

        size_t off = 4;

        while (off + 16 <= len)
        {
            RouterLinkV3 link;
            link.type = buf[off];
            link.metric = utils::readU16(buf + off + 2);
            link.interfaceId = utils::readU32(buf + off + 4);
            link.neighborInterfaceId = utils::readU32(buf + off + 8);
            link.neighborRouterId = utils::readU32(buf + off + 12);
            lsa.links.push_back(link);
            off += 16;
        }

        if (off != len) return std::nullopt;
        return lsa;
    }

    /**
     * @brief Serializes the RouterLsaV3 into a wire-format buffer.
     *
     * Writes options and all link entries. Validates buffer length before
     * writing.
     *
     * @param buf Buffer to write serialized data.
     * @param len Total buffer length.
     * @return True if serialization succeeds, false if buffer too small.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != (4 + (16 * links.size()))) return false;

        utils::writeU32(buf, options);

        size_t off = 4;
        for (const auto& link : links)
        {
            buf[off++] = link.type; 
            buf[off++] = 0;
            utils::writeU16(buf + off, link.metric); off += 2;
            utils::writeU32(buf + off, link.interfaceId); off += 4;
            utils::writeU32(buf + off, link.neighborInterfaceId); off += 4;
            utils::writeU32(buf + off, link.neighborRouterId); off += 4;
        }

        return true;
    }

    /**
     * @brief Computes the total serialized size of the Router LSA.
     *
     * Includes 4-byte header plus 16 bytes per link.
     *
     * @return Length in bytes needed for serialization.
     */
    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(16 * links.size());
    }

    /**
     * @brief Appends Router LSA fields to a Fletcher checksum.
     *
     * Used to validate integrity before transmission or storage.
     *
     * @param check Checksum object to append fields to.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(options);
        for (const auto& link : links)
        {
            check.add(link.type);
            check.addU16(link.metric);
            check.addU32(link.interfaceId);
            check.addU32(link.neighborInterfaceId);
            check.addU32(link.neighborRouterId);
        }
    }

    /**
     * @brief Compares two RouterLsaV3 instances for equality.
     *
     * Options and all links must match. Links are sorted before comparison to
     * ensure order-independent equality.
     *
     * @param rhs LSA to compare against.
     * @return True if LSAs are identical.
     */
    bool operator==(const RouterLsaV3& rhs) const
    {
        if (options != rhs.options || links.size() != rhs.links.size())
            return false;

        std::vector<uint16_t> a(links.size()), b(rhs.links.size());

        std::iota(a.begin(), a.end(), 0);
        std::iota(b.begin(), b.end(), 0);

        std::sort(a.begin(), a.end(), [&](uint16_t i, uint16_t j) { return links[i] < links[j]; });
        std::sort(b.begin(), b.end(), [&](uint16_t i, uint16_t j) { return rhs.links[i] < rhs.links[j]; });

        for (size_t k = 0; k < a.size(); ++k)
            if (!(links[a[k]] == rhs.links[b[k]]))
                return false;

        return true;
    }
};

} // namespace routing::ospf

#endif // ROUTER_LSA_V3_HPP
