/**
 * @file UdpHeader.hpp
 */

// UdpHeader.hpp

#ifndef UDP_HEADER_HPP
#define UDP_HEADER_HPP

#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"

#define UDP_DHCP_CLIENT   0x0044U ///< UDP source port for DHCP
#define UDP_DHCP_SERVER   0x0043U ///< UDP destination port for DHCP
#define UDP_DHCPV6_CLIENT 0x0222U ///< UDP source port for DHCPv6
#define UDP_DHCPV6_SERVER 0x0223U ///< UDP destination port for DHCPv6

namespace packet
{

/**
 * @struct UdpHeaderRaw
 * @brief Represents a raw UDP header.
 */
#pragma pack(push, 0)
struct UdpHeaderRaw
{
    uint8_t sourcePort[2];
    uint8_t destinationPort[2];
    uint8_t length[2];
    uint8_t checksum[2];
};
#pragma pack(pop)

/**
 * @struct UdpHeader
 * @brief Represents a UDP (User Datagram Protocol) header.
 */
struct UdpHeader
{
    DEFINE_FIXED_HEADER(UdpHeaderRaw);

    uint16_t getSourcePort() const
        { return utils::readU16(raw->sourcePort); }
    uint16_t getDestinationPort() const
        { return utils::readU16(raw->destinationPort); }
    uint16_t getLength() const
        { return utils::readU16(raw->length); }
    const uint8_t* getChecksum() const
        { return raw->checksum; }
    
    void setChecksum(uint8_t* val)
        { std::memcpy(raw->checksum, val, 2); }
    void setSourcePort(uint16_t val)
        { utils::writeU16(raw->sourcePort, val); }
    void setDestinationPort(uint16_t val)
        { utils::writeU16(raw->destinationPort, val); }
};

} // namespace packet

#endif // UDP_HEADER_HPP

