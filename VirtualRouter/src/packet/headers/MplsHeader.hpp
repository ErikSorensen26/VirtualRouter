/**
 * @file MplsHeader.hpp
 */

// MplsHeader.hpp

#ifndef MPLS_HEADER_HPP
#define MPLS_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/**
 * @struct MplsHeaderRaw
 * @brief Represents a raw MPLS header
 */
#pragma pack(push, 0)
struct MplsHeaderRaw
{
    uint8_t bytes[4];
};
#pragma pack(pop)

/**
 * @struct MplsHeader
 * @brief Represents an MPLS (Multiprotocol Label Switching) header.
 */
struct MplsHeader
{
    DEFINE_FIXED_HEADER(MplsHeaderRaw);

    uint32_t getLabel() const {
        return (static_cast<uint32_t>(raw->bytes[0]) << 12) |
               (static_cast<uint32_t>(raw->bytes[1]) << 4) |
               (static_cast<uint32_t>(raw->bytes[2]) >> 4);
    }
    uint8_t getExp() const
         { return (raw->bytes[2] >> 1) & 0x07; }
    bool getBottomOfStack() const
        { return raw->bytes[2] & 0x01; }
    uint8_t getTtl() const
        { return raw->bytes[3]; }

    void setLabel(uint32_t label) 
        { raw->bytes[0] = static_cast<uint8_t>((label >> 12) & 0xFF);
          raw->bytes[1] = static_cast<uint8_t>((label >> 4) & 0xFF);
          raw->bytes[2] = static_cast<uint8_t>(((label & 0x0F) << 4) | (raw->bytes[2] & 0x0F)); }
    void setExp(uint8_t exp)
        { raw->bytes[2] = static_cast<uint8_t>((raw->bytes[2] & 0xF1) | ((exp & 0x07) << 1)); }
    void setBottomOfStack(bool bos)
        { raw->bytes[2] = static_cast<uint8_t>((raw->bytes[2] & 0xFE) | (bos ? 0x01 : 0x00)); }
    void setTtl(uint8_t ttl)
        { raw->bytes[3] = ttl; }
};

} // namespace packet

#endif // MPLS_HEADER_HPP

