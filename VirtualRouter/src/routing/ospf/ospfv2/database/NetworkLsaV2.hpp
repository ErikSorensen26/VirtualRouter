/**
 * @file NetworkLsaV2.hpp
 * @brief OSPFv2 Network LSA (Type 2) body — RFC 2328 §A.4.3.
 */

#ifndef NETWORK_LSA_V2_HPP
#define NETWORK_LSA_V2_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <algorithm>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
/**
 * @brief Wire-format body of an OSPFv2 Network LSA (Type 2).
 * @ingroup OSPF_V2_DATABASE
 *
 * Originated by the Designated Router (DR) on a multi-access network.
 * Contains the subnet mask of the transit link and the list of fully-adjacent
 * routers (including the DR itself), as defined in RFC 2328 §A.4.3.
 *
 * Body length is variable: `4 + 4 * attachedRouters.size()` bytes.
 *
 * Use @ref build to parse a received buffer, and @ref buildBody to serialise
 * for transmission.  Equality comparison is order-independent (router lists
 * are sorted before comparison).
 */
struct NetworkLsaV2
{
    uint32_t networkMask;                   ///< Subnet mask of the transit network.
    std::vector<uint32_t> attachedRouters;  ///< Router IDs of all fully-adjacent routers on the link.

    /**
     * @brief Parses an OSPFv2 Network LSA body from a wire buffer.
     * @param buf Pointer to the start of the LSA body (after the 20-byte LSA header).
     * @param len Length of @p buf in bytes; must be at least 4 and a multiple of 4.
     * @return Parsed struct on success, or @c std::nullopt if the buffer is malformed.
     */
    static std::optional<NetworkLsaV2> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 4) return std::nullopt;

        NetworkLsaV2 lsa;

        lsa.networkMask = utils::read<uint32_t>(buf);
        size_t offset = 4;

        if ((len - offset) % 4 != 0)
            return std::nullopt;

        while (offset + 4 <= len)
        {
            uint32_t rid = utils::read<uint32_t>(buf + offset);
            lsa.attachedRouters.push_back(rid);
            offset += 4;
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
        if ((attachedRouters.size() * 4) + 4 != len)
            return false;

        utils::write<uint32_t>(buf, networkMask);
        size_t off = 4;
        for (auto& r : attachedRouters)
        {
            utils::write<uint32_t>(buf + off, r);
            off += 4;
        }
        return true;
    }

    /**
     * @brief Returns the serialised size of this LSA body in bytes.
     * @return `4 + 4 * attachedRouters.size()`.
     */
    inline uint16_t size() const
    {
        return static_cast<uint16_t>(4 + (4 * attachedRouters.size()));
    }

    /**
     * @brief Feeds this LSA body's fields into a Fletcher checksum accumulator.
     * @param check Checksum accumulator to update.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU32(networkMask);
        for (auto& r : attachedRouters)
            check.addU32(r);
    }

    /**
     * @brief Compares two Network LSAs for semantic equality.
     *
     * The comparison is order-independent: both router lists are sorted before
     * being compared so that OSPF flooding decisions are not affected by the
     * order in which neighbors were added to the list.
     *
     * @param lsa The other Network LSA to compare against.
     * @return True if both LSAs describe the same network mask and router set.
     */
    bool operator==(const NetworkLsaV2& lsa) const
    {
        if (attachedRouters.size() != lsa.attachedRouters.size() || networkMask != lsa.networkMask)
            return false;
        auto a = attachedRouters;
        auto b = lsa.attachedRouters;

        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());

        return a == b;
    }
};
} // namespace routing::ospf

#endif // NETWORK_LSA_V2_HPP
