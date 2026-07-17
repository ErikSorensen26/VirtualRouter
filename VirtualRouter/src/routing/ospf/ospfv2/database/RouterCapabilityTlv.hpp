/**
 * @file RouterCapabilityTlv.hpp
 * @brief Router Informational Capabilities TLV carried inside an OSPFv2 Router Capability opaque LSA.
 */

#ifndef OSPF_ROUTER_CAPABILITY_TLV_HPP
#define OSPF_ROUTER_CAPABILITY_TLV_HPP

#include <cstdint>
#include <optional>
#include <ByteUtils.hpp>

namespace routing::ospf
{
/// Bit 0 of the Router Informational Capabilities bitmask: graceful restart capable.
static constexpr uint32_t ROUTER_CAP_GRACEFUL_RESTART = 0x00000001;

/**
 * @brief Single TLV carrying the Router Informational Capabilities bitmask (RFC 4970 §2).
 * @ingroup OSPF_V2_DATABASE
 *
 * Encoded as TLV type 1, length 4, followed by the 32-bit capabilities value —
 * 8 bytes total. This is the sole TLV populated inside the Router Capability
 * opaque LSA (RFC 5250 opaque type 4 wire value; see @ref OpaqueOriginatorV2).
 */
struct RouterCapabilityTlv
{
    static constexpr uint16_t TLV_TYPE = 1;

    uint32_t capabilities = 0; ///< Bitmask of ROUTER_CAP_* flags.

    /**
     * @brief Parses a Router Capability TLV from a raw buffer.
     * @param buf Pointer to the start of the 8-byte TLV (type+len+value).
     * @param len Available bytes in @p buf.
     * @return Decoded TLV, or std::nullopt if @p len is too small or the type/length fields don't match.
     */
    static std::optional<RouterCapabilityTlv> build(const uint8_t* buf, uint16_t len)
    {
        if (len < 8) return std::nullopt;

        uint16_t type = utils::readU16(buf);
        uint16_t tlvLen = utils::readU16(buf + 2);
        if (type != TLV_TYPE || tlvLen != 4) return std::nullopt;

        RouterCapabilityTlv tlv;
        tlv.capabilities = utils::readU32(buf + 4);
        return tlv;
    }

    /// Serialised size in bytes (4-byte TLV header + 4-byte value).
    static constexpr uint16_t size() { return 8; }

    /**
     * @brief Serialises this TLV into a wire buffer.
     * @param buf Destination buffer; must be at least @ref size() bytes.
     * @param len Available bytes in @p buf.
     * @return True on success; false if @p len is smaller than @ref size().
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < size()) return false;
        utils::writeU16(buf, TLV_TYPE);
        utils::writeU16(buf + 2, 4);
        utils::writeU32(buf + 4, capabilities);
        return true;
    }
};
} // namespace routing::ospf

#endif // OSPF_ROUTER_CAPABILITY_TLV_HPP
