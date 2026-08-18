/**
 * @file InterAreaRouterLsa.hpp
 * @brief OSPFv3 Inter-Area-Router LSA format.
 */

#ifndef INTER_AREA_ROUTER_LSA_HPP
#define INTER_AREA_ROUTER_LSA_HPP

#include <cstdint>
#include <optional>
#include <ByteUtils.hpp>

#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{

/**
 * @brief OSPFv3 Inter-Area-Router LSA body.
 * @ingroup OSPF_V3_DATABASE
 *
 * Advertises reachability to an ABR in another area.
 * Carries:
 * - Destination Router ID
 * - Cost to reach that router
 * - Router capability/options field
 *
 * Wire format is fixed-length (12 bytes) with reserved bytes
 * that MUST be zero.
 */
struct InterAreaRouterLsa
{
    uint32_t options;              ///< 24-bit router options field.
    uint32_t metric;               ///< 24-bit cost to destination router.
    uint32_t destinationRouterId;  ///< Target router ID.

    /**
     * @brief Parse an Inter-Area-Router LSA body.
     *
     * Validates exact size and enforces zeroed reserved fields.
     *
     * @param buf Input buffer.
     * @param len Buffer length (must be 12).
     * @return Parsed LSA, or nullopt if the length or reserved bytes don't match.
     */
    static std::optional<InterAreaRouterLsa> build(const uint8_t* buf, uint16_t len)
    {
        if (len != 12) return std::nullopt;

        InterAreaRouterLsa lsa;

        if (buf[0] != 0) return std::nullopt;
        lsa.options = utils::read<uint32_t, 3>(buf + 1);

        if (buf[4] != 0) return std::nullopt;
        lsa.metric = utils::read<uint32_t, 3>(buf + 5);

        lsa.destinationRouterId = utils::read<uint32_t>(buf + 8);

        return lsa;
    }

    /**
     * @brief Serialize the LSA body into a buffer.
     *
     * Writes fixed fields with required zeroed reserved bytes.
     *
     * @param[out] buf Output buffer.
     * @param len Buffer size (must be 12).
     * @return True on success, false if size mismatch.
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len != 12) return false;
        
        buf[0] = 0;
        utils::write<uint32_t, 3>(buf + 1, options);
        buf[4] = 0;
        utils::write<uint32_t, 3>(buf + 5, metric);
        utils::write<uint32_t>(buf + 8, destinationRouterId);

        return true;
    }

    /**
     * @brief Fixed serialized size of the LSA body.
     *
     * @return Always 12 bytes.
     */
    static constexpr uint16_t size()
    {
        return 12;
    }

    /**
     * @brief Append fields to Fletcher checksum.
     *
     * Adds fields in wire order, excluding reserved bytes.
     *
     * @param[in,out] check Checksum accumulator.
     */
    void appendChecksum(ChecksumFletcher& check) const
    {
        check.addU24(options);
        check.addU24(metric);
        check.addU32(destinationRouterId);
    }

    /**
     * @brief Equality comparison (logical content).
     *
     * Compares metric and destination only; options are ignored.
     */
    bool operator==(const InterAreaRouterLsa& rhs) const
    {
        return metric == rhs.metric && destinationRouterId == rhs.destinationRouterId;
    }
};

} // namespace routing::ospf

#endif // INTER_AREA_ROUTER_LSA_HPP
