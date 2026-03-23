// EthernetHeader.hpp

#ifndef ETHERNET_HEADER_HPP
#define ETHERNET_HEADER_HPP

#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"

#define ETHERNET_ARP    0x0806      ///< EtherType for ARP
#define ETHERNET_IPV4   0x0800      ///< EtherType for IPv4
#define ETHERNET_IPV6   0x86DD      ///< EtherType for IPv6
#define ETHERNET_MPLS   0x8847      ///< EtherType for MPLS
#define ETHERNET_VLAN   0x8100      ///< EtherType for VLAN
#define ETHERNET_LLDP   0x88CC      ///< EtherType for LLDP

inline constexpr uint8_t ETHERNET_MAC_BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; ///< Broadcast MAC address (FF:FF:FF:FF:FF:FF).
inline constexpr uint8_t ETHERNET_MAC_SOURCE[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};    ///< Placeholder source MAC address (00:00:00:00:00:00).
                                                                                                                   ///
namespace packet
{
/**
 * @struct EthernetHeaderRaw
 * @brief Represents a raw Ethernet header.
 */
#pragma pack(push, 1)
struct EthernetHeaderRaw
{
    uint8_t destinationMac[6];
    uint8_t sourceMac[6];
    uint8_t type[2];
};
#pragma pack(pop)

/**
 * @struct EthernetHeader
 * @brief Represents an Ethernet frame header.
 */
struct EthernetHeader
{
    DEFINE_FIXED_HEADER(EthernetHeaderRaw);

    void setSourceMac(uint64_t val)
        { utils::writeU48(raw->sourceMac, val); }
    void setDestinationMac(uint64_t val)
        { utils::writeU48(raw->destinationMac, val); }
    void setType(const uint16_t val)
        { utils::writeU16(raw->type, val); }
};

} // namespace packet

#endif // ETHERNET_HEADER_HPP

