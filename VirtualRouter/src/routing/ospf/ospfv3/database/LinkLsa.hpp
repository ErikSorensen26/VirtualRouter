/**
 * @file LinkLsa.hpp
 * @brief OSPFv3 Link LSA format and flooding.
 */

#ifndef LINK_LSA_HPP
#define LINK_LSA_HPP

#include <IPAddress.h>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{

/**
 * @brief One IPv6 prefix entry inside a Link-LSA.
 * @ingroup OSPF_V3_DATABASE
 *
 * Represents a single prefix advertised on the link by the originating
 * router. Used by neighbors to derive on-link addressing information.
 *
 * Contains:
 * - Prefix options (OSPFv3 flags)
 * - Variable-length IPv6 prefix
 *
 * Created during parsing and serialized as part of LinkLsa.
 */
struct LinkLsaPrefix
{
    uint8_t options;           ///< OSPFv3 prefix flags.
    types::IPv6Prefix prefix;  ///< IPv6 prefix with explicit length.
};

/**
 * @brief OSPFv3 Link-LSA body.
 * @ingroup OSPF_V3_DATABASE
 *
 * Encodes per-link information originated by a router:
 * - Link-local IPv6 address
 * - Interface priority (DR election)
 * - Advertised prefixes
 *
 * Acts as the boundary between raw packet buffers and internal state.
 * Does not manage flooding, aging, or LSDB insertion.
 *
 * Parsing and serialization must strictly follow wire format ordering.
 */
struct LinkLsa
{
    uint8_t priority;                     ///< DR election priority.
    uint32_t options;                     ///< 24-bit OSPFv3 options.
    types::IPv6Address localLink;         ///< Link-local IPv6 address.
    std::vector<LinkLsaPrefix> prefixes;  ///< Advertised prefixes.

    /**
     * @brief Parse a Link-LSA body from a buffer.
     *
     * Validates minimum size and walks the prefix list with strict
     * bounds checking. Prefix length is in bits and converted to bytes.
     *
     * @param buf Input buffer.
     * @param len Buffer length.
     * @return Parsed LSA or nullopt on failure.
     */
    static std::optional<LinkLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 21) return std::nullopt;

        LinkLsa lsa;

        lsa.priority = buf[0];
        lsa.options = utils::read<uint32_t, 3>(buf + 1);
        lsa.localLink = types::IPv6Address(buf + 4);

        uint8_t prefixList = buf[20];
        size_t off = 21;
        for (uint8_t i = 0; i < prefixList; i++)
        {
            if (off + 2 > len) return std::nullopt;
            LinkLsaPrefix link;
            uint8_t prefixLen = buf[off++];
            link.options = buf[off++];
            uint8_t prefixBytes = (prefixLen + 7) / 8;

            if (off + prefixBytes > len) return std::nullopt;
            link.prefix = types::IPv6Prefix(buf + off, prefixLen);
            lsa.prefixes.push_back(link);
        }

        return lsa;
    }

    /**
     * @brief Serialize the Link-LSA body into a buffer.
     *
     * Writes fixed fields followed by prefix entries. Caller must provide
     * sufficient space (see size()).
     *
     * @param[out] buf Output buffer.
     * @param len Buffer size.
     * @return True on success, false if buffer too small.
     *
     * @warning No rollback on failure; buffer contents undefined.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 21) return false;

        buf[0] = priority;
        utils::write<uint32_t, 3>(buf + 1, options);
        utils::write<__uint128_t>(buf + 4, localLink.addr);

        buf[20] = static_cast<uint8_t>(prefixes.size());
        size_t off = 21;
        for (const auto& link : prefixes)
        {
            if (off + 2 > len) return false;
            buf[off++] = link.prefix.prefixLength;
            buf[off++] = link.options;
            uint8_t prefixBytes = (link.prefix.prefixLength + 7) / 8;

            if (off + prefixBytes > len) return false;
            utils::write<__uint128_t>(buf + off, link.prefix.addr, prefixBytes);
        }

        return true;
    }

    /**
     * @brief Compute serialized size of the LSA body.
     *
     * Includes fixed header and all prefix entries using
     * ceil(prefixLen / 8) byte sizing.
     *
     * @return Total size in bytes.
     */
    inline uint16_t size() const
    {
        uint16_t len = 21;
        for (const auto& link : prefixes)
        {
            len += 2 + ((link.prefix.prefixLength + 7) / 8);
        }
        return len;
    }

    /**
     * @brief Append fields to Fletcher checksum.
     *
     * Fields are added in wire order to match serialization.
     *
     * @param[in,out] check Checksum accumulator.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.add(priority);
        check.addU24(options);
        check.addBytes(localLink.raw(), 16);
        for (const auto& link : prefixes)
        {
            check.add(link.prefix.prefixLength);
            check.add(link.options);
            uint8_t prefixBytes = (link.prefix.prefixLength + 7) / 8;
            check.addBytes(link.prefix.raw(), prefixBytes);
        }
    }
};

} // namespace routing::ospf

#endif // LINK_LSA_HPP
