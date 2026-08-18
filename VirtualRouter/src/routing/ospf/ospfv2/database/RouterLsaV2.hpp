/**
 * @file RouterLsaV2.hpp
 * @brief OSPFv2 Router LSA (Type 1) body and per-link descriptor — RFC 2328 §A.4.2.
 */

/**
 * @defgroup OSPF_V2_DATABASE OSPFv2 Database
 * @ingroup OSPF_V2
 * @brief OSPFv2 LSA type definitions: Router, Network, Summary, External, Opaque.
 */

#ifndef ROUTER_LSA_V2_HPP
#define ROUTER_LSA_V2_HPP

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
 * @brief A single link descriptor within an OSPFv2 Router LSA body.
 * @ingroup OSPF_V2_DATABASE
 *
 * Encodes one adjacency or stub network, as defined in RFC 2328 §A.4.2.
 * Link types:
 * - 1 — Point-to-point connection to another router.
 * - 2 — Connection to a transit network (DR exists).
 * - 3 — Connection to a stub network.
 * - 4 — Virtual link.
 *
 * TOS sub-fields are parsed but discarded (only metric[0] is kept).
 */
struct RouterLinkV2
{
    uint32_t linkId;    ///< Link ID (neighbor RID, DR IP, or subnet address depending on type).
    uint32_t linkData;  ///< Link data (interface IP or unnumbered interface index).
    uint8_t type;       ///< Link type (1=P2P, 2=transit, 3=stub, 4=virtual).
    uint16_t metric;    ///< Cost metric for this link.

    /**
     * @brief Compares two link descriptors for equality.
     * @param rhs The other link to compare against.
     * @return True if all fields are identical.
     */
    bool operator==(const RouterLinkV2& rhs) const noexcept
    {
        return linkId == rhs.linkId &&
               linkData == rhs.linkData &&
               type == rhs.type &&
               metric == rhs.metric;
    }

    /**
     * @brief Provides a total order over link descriptors for sort-based equality checks.
     * @param rhs The other link to compare against.
     * @return True if this link is ordered before @p rhs.
     */
    bool operator<(const RouterLinkV2& rhs) const noexcept
    {
        return std::tie(type, linkId, linkData, metric)
             < std::tie(rhs.type, rhs.linkId, rhs.linkData, rhs.metric);
    }
};

/**
 * @brief Wire-format body of an OSPFv2 Router LSA (Type 1).
 * @ingroup OSPF_V2_DATABASE
 *
 * Every router in an OSPF area originates exactly one Router LSA describing
 * all of its active links.  The body begins with a flags/count header
 * followed by a variable number of @ref RouterLinkV2 descriptors.
 *
 * Body length is `4 + 12 * links.size()` bytes (TOS entries are not generated).
 *
 * Use @ref build to parse a received buffer, and @ref buildBody to serialise
 * for transmission.  Equality comparison is order-independent.
 */
struct RouterLsaV2
{
    uint8_t flags;                   ///< Router flags (bit 0=ASBR, bit 1=ABR, bit 2=Vlink endpoint).
    std::vector<RouterLinkV2> links; ///< Ordered list of link descriptors.

    /**
     * @brief Parses an OSPFv2 Router LSA body from a wire buffer.
     * @param buf Pointer to the start of the LSA body (after the 20-byte LSA header).
     * @param len Length of @p buf in bytes; must be at least 4.
     * @return Parsed struct on success, or @c std::nullopt if the buffer is malformed.
     */
    static std::optional<RouterLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        RouterLsaV2 lsa;

        lsa.flags = buf[0];
        uint16_t linkNum = utils::read<uint16_t>(buf + 2);

        size_t offset = 4;

        for (int i = 0; i < linkNum; i++)
        {
            if (offset + 12 > len) return std::nullopt;

            RouterLinkV2 link;
            link.linkId = utils::read<uint32_t>(buf + offset); offset += 4;
            link.linkData = utils::read<uint16_t>(buf + offset); offset += 4;

            link.type = buf[offset++];
            uint8_t tosCount = buf[offset++];

            link.metric = utils::read<uint16_t>(buf + offset); offset += 2;

            size_t tosBytes = static_cast<size_t>(tosCount) * 4;
            if (offset + tosBytes > len) return std::nullopt;

            offset += tosBytes;

            lsa.links.push_back(link);
        }

        return lsa;
    }

    /**
     * @brief Serialises this LSA body into a wire buffer.
     * @param buf Destination buffer; must be at least @ref size() bytes.
     * @param len Available bytes in @p buf; must equal @ref size().
     * @return True on success; false if @p len does not match the expected size.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if ((links.size() * 12) + 4 != len) return false;

        buf[0] = flags;
        buf[1] = 0;
        utils::write<uint16_t>(buf + 2, static_cast<uint16_t>(links.size()));

        size_t off = 4;
        for (auto& link : links)
        {
            utils::write<uint32_t>(buf + off, link.linkId);
            utils::write<uint32_t>(buf + off + 4, link.linkData);
            buf[off + 8] = link.type;
            buf[off + 9] = 0;
            utils::write<uint16_t>(buf + off + 10, link.metric);
            off += 12;
        }
        return true;
    }

    /**
     * @brief Returns the serialised size of this LSA body in bytes.
     * @return `4 + 12 * links.size()`.
     */
    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(12 * links.size());
    }

    /**
     * @brief Feeds this LSA body's fields into a Fletcher checksum accumulator.
     * @param check Checksum accumulator to update.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.add(flags);
        // Next byte is 0
        check.addU16(static_cast<uint16_t>(links.size()));
        for (const auto& link : links)
        {
            check.addU32(link.linkId);
            check.addU32(link.linkData);
            check.add(link.type);
            // Next byte is 0
            check.addU16(link.metric);
        }
    }

    /**
     * @brief Compares two Router LSAs for semantic equality.
     *
     * Both link lists are sorted by their natural order before comparison so
     * that the result is independent of link insertion order.
     *
     * @param rhs The other Router LSA to compare against.
     * @return True if both LSAs have the same flags and identical link sets.
     */
    bool operator==(const RouterLsaV2& rhs) const
    {
        if (flags != rhs.flags || links.size() != rhs.links.size())
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

#endif // ROUTER_LSA_V2_HPP
