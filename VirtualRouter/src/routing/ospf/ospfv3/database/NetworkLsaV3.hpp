/**
 * @file NetworkLsaV3.hpp
 * @brief OSPFv3 Network LSA body: routers attached to a multi-access network segment.
 */

#ifndef NETWORK_LSA_V3_HPP
#define NETWORK_LSA_V3_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <algorithm>
#include <ByteUtils.hpp>
#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{

/**
 * @brief OSPFv3 Network LSA body: routers attached to a multi-access network segment.
 * @ingroup OSPF_V3_DATABASE
 *
 * Originated by the segment's Designated Router. Stores the 24-bit options
 * field and the list of attached router IDs; owns its router vector and is
 * safe to copy and compare.
 */
struct NetworkLsaV3
{
    uint32_t options;                 ///< 24-bit options field stored in 32-bit.
    std::vector<uint32_t> attachedRouters; ///< List of attached router IDs.

    /**
     * @brief Parses a raw buffer into a NetworkLsaV3 instance.
     *
     * Validates buffer length and alignment for router entries. Returns
     * std::nullopt if the buffer is truncated or malformed.
     *
     * @param buf Pointer to raw buffer.
     * @param len Length of the buffer in bytes.
     * @return Optional NetworkLsaV3 if parsing succeeds, nullopt if fails.
     */
    static std::optional<NetworkLsaV3> build(const uint8_t* buf, uint16_t len) {
        if (len < 4) return std::nullopt;

        NetworkLsaV3 lsa;
        lsa.options = utils::read<uint32_t, 3>(buf + 1);
        size_t off = 4;

        if ((len - off) % 4 != 0) return std::nullopt;

        while (off < len)
        {
            lsa.attachedRouters.push_back(utils::read<uint32_t>(buf + off));
            off += 4;
        }

        return lsa;
    }

    /**
     * @brief Serializes the NetworkLsaV3 into a wire-format buffer.
     *
     * Writes options and attached router IDs. Validates buffer length before
     * writing.
     *
     * @param buf Buffer to write serialized data.
     * @param len Total buffer length in bytes.
     * @return True if serialization succeeds, false if buffer too small.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < 4) return false;

        utils::write<uint32_t, 3>(buf + 1, options);
        size_t off = 4;

        if (4 + (4 * attachedRouters.size()) != len) return false;

        for (const auto& router : attachedRouters)
        {
            utils::write<uint32_t>(buf + off, router);
            off += 4;
        }

        return true;
    }

    /// Serialised size in bytes: `4 + 4 * attachedRouters.size()`.
    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(4 * attachedRouters.size());
    }

    /// Folds this LSA body's fields into @p check for OSPF LSA checksum computation.
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU24(options);
        for (const auto& router : attachedRouters)
            check.addU32(router);
    }

    /**
     * @brief Compares two NetworkLsaV3 instances for equality.
     *
     * Checks options and attached routers. Attached routers are sorted
     * before comparison to ensure order-independent equality.
     *
     * @param lsa LSA to compare.
     * @return True if options and routers match, false otherwise.
     */
    bool operator==(const NetworkLsaV3& lsa) const
    {
        if (attachedRouters.size() != lsa.attachedRouters.size() || options != lsa.options)
            return false;

        auto a = attachedRouters;
        auto b = lsa.attachedRouters;

        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());

        return a == b;
    }
};

} // namespace routing::ospf

#endif // NETWORK_LSA_V3_HPP
