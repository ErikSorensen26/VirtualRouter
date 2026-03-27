/**
 * @file NetworkLsaV3.hpp
 * @brief OSPFv3 Network LSA representation and wire-format handling.
 *
 * Defines the NetworkLsaV3 structure, which represents OSPFv3 Network LSAs.
 * Supports parsing from buffers, serialization, size calculation, checksum
 * computation, and equality comparison. Attached routers are stored in a
 * vector and sorted when comparing equality.
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
 * @brief Represents an OSPFv3 Network LSA containing attached routers.
 * @ingroup OSPF_V3_DATABASE
 *
 * Stores the 24-bit options field and a list of attached routers. Provides
 * parsing from wire buffers, serialization, size computation, and checksum
 * integration.
 *
 * ## Architectural Role
 * Used in flooding and database storage to advertise all routers attached
 * to a multi-access network segment.
 *
 * ## Lifecycle & Ownership
 * Constructed via `build()` or manually populated. Owns its vector of attached
 * routers; safe to copy and compare.
 *
 * @warning Buffer length must be consistent with 4-byte router entries.
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
        lsa.options = utils::readU24(buf + 1);
        size_t off = 4;

        if ((len - off) % 4 != 0) return std::nullopt;

        while (off < len)
        {
            lsa.attachedRouters.push_back(utils::readU32(buf + off));
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

        utils::writeU24(buf + 1, options);
        size_t off = 4;

        if (4 + (4 * attachedRouters.size()) != len) return false;

        for (const auto& router : attachedRouters)
        {
            utils::writeU32(buf + off, router);
            off += 4;
        }

        return true;
    }

    /**
     * @brief Calculates the total serialized size of the LSA.
     *
     * Includes 4-byte header plus 4 bytes per attached router.
     *
     * @return Length in bytes needed for serialization.
     */
    inline uint16_t size() const
    {
        return 4 + static_cast<uint16_t>(4 * attachedRouters.size());
    }

    /**
     * @brief Appends LSA fields to a Fletcher checksum.
     *
     * Used to validate integrity before transmission or storage.
     *
     * @param check Checksum object to append fields to.
     */
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
