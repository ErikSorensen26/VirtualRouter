/**
 * @file GraceLsa.hpp
 * @brief OSPFv2 Grace-LSA TLV format for graceful restart (RFC 3623 SS3).
 */

#ifndef OSPF_GRACE_LSA_HPP
#define OSPF_GRACE_LSA_HPP
#include <cstdint>
#include <optional>
#include <ByteUtils.hpp>
namespace routing::ospf
{
/// Link-scope opaque type for the Grace-LSA (RFC 3623 SS3).
static constexpr uint8_t GRACE_LSA_OPAQUE_TYPE = 3;

/// Restart reason code carried in the Grace-LSA's Grace Reason TLV (RFC 3623 SS3).
enum class GraceRestartReason : uint8_t
{
    UNKNOWN = 0,
    SOFTWARE_RESTART = 1,
    SOFTWARE_UPGRADE = 2,
    CONTROL_PROCESSOR_SWITCHOVER = 3,
};

/**
 * @brief Body of an OSPFv2 Grace-LSA: Grace Period, Grace Reason, and optional restart IP TLVs (RFC 3623 SS3).
 * @ingroup OSPF_V2_DATABASE
 */
struct GraceLsaTlv
{
    static constexpr uint16_t GRACE_PERIOD_TYPE = 1; ///< TLV type for the mandatory Grace Period TLV.
    static constexpr uint16_t GRACE_REASON_TYPE = 2; ///< TLV type for the mandatory Grace Reason TLV.
    static constexpr uint16_t GRACE_IP_TYPE = 3;      ///< TLV type for the optional restart IP address TLV.

    uint32_t gracePeriodSeconds = 0;                        ///< Grace period advertised to neighbors, in seconds.
    GraceRestartReason restartReason = GraceRestartReason::UNKNOWN; ///< Restart reason code.
    uint32_t restartIpAddress = 0;                          ///< Optional restart IP address; 0 if the TLV is omitted.

    /**
     * @brief Parses a Grace-LSA TLV set from a raw buffer.
     * @param buf Pointer to the start of the TLV block.
     * @param len Available bytes in @p buf.
     * @return Decoded TLV set, or std::nullopt if malformed or missing the mandatory Grace Period TLV.
     */
    static std::optional<GraceLsaTlv> build(const uint8_t* buf, uint16_t len)
    {
        GraceLsaTlv tlv;
        bool sawPeriod = false;

        uint16_t offset = 0;
        while (offset + 4 <= len)
        {
            uint16_t type = utils::read<uint16_t>(buf + offset);
            uint16_t tlvLen = utils::read<uint16_t>(buf + offset + 2);
            uint16_t valueOffset = offset + 4;
            if (valueOffset + tlvLen > len) return std::nullopt;

            if (type == GRACE_PERIOD_TYPE && tlvLen == 4)
            {
                tlv.gracePeriodSeconds = utils::read<uint32_t>(buf + valueOffset);
                sawPeriod = true;
            }
            else if (type == GRACE_REASON_TYPE && tlvLen == 1)
            {
                tlv.restartReason = static_cast<GraceRestartReason>(buf[valueOffset]);
            }
            else if (type == GRACE_IP_TYPE && tlvLen == 4)
            {
                tlv.restartIpAddress = utils::read<uint32_t>(buf + valueOffset);
            }

            // TLVs are padded to 4-byte alignment (RFC 3623 SS3 references RFC 2370's TLV format).
            offset = valueOffset + ((tlvLen + 3) & ~static_cast<uint16_t>(3));
        }

        if (!sawPeriod) return std::nullopt;
        return tlv;
    }

    /// Serialised size in bytes: Grace Period TLV + Grace Reason TLV, plus the restart IP TLV if present.
    uint16_t size() const { return 8 + 8 + (hasRestartIp() ? 8 : 0); }

    /// True if a restart IP address TLV should be included when serialising.
    bool hasRestartIp() const { return restartIpAddress != 0; }

    /**
     * @brief Serialises this TLV set into a wire buffer.
     * @param buf Destination buffer; must be at least @ref size() bytes.
     * @param len Available bytes in @p buf.
     * @return True on success; false if @p len is smaller than @ref size().
     */
    bool buildBody(uint8_t* buf, uint16_t len) const
    {
        if (len < size()) return false;

        uint16_t offset = 0;
        utils::write<uint16_t>(buf + offset, GRACE_PERIOD_TYPE);
        utils::write<uint16_t>(buf + offset + 2, 4);
        utils::write<uint32_t>(buf + offset + 4, gracePeriodSeconds);
        offset += 8;

        utils::write<uint16_t>(buf + offset, GRACE_REASON_TYPE);
        utils::write<uint16_t>(buf + offset + 2, 1);
        buf[offset + 4] = static_cast<uint8_t>(restartReason);
        buf[offset + 5] = 0;
        buf[offset + 6] = 0;
        buf[offset + 7] = 0;
        offset += 8;

        if (hasRestartIp())
        {
            utils::write<uint16_t>(buf + offset, GRACE_IP_TYPE);
            utils::write<uint16_t>(buf + offset + 2, 4);
            utils::write<uint32_t>(buf + offset + 4, restartIpAddress);
            offset += 8;
        }

        return true;
    }
};
} // namespace routing::ospf
#endif // OSPF_GRACE_LSA_HPP
