/**
 * @file ArpHeader.hpp
 */

// ArpHeader.hpp

#ifndef ARP_HEADER_HPP
#define ARP_HEADER_HPP

#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"

#define ARP_HARDWARE_ETHERNET       0x0001      ///< ARP hardware type for Ethernet

#define ARP_OPCODE_REQUEST          0x0001      ///< ARP Request
#define ARP_OPCODE_REPLY            0x0002      ///< ARP Reply
#define ARP_OPCODE_REVERSE_REQUEST  0x0003      ///< Reverse ARP Request
#define ARP_OPCODE_REVERSE_REPLY    0x0004      ///< Reverse ARP Reply
#define ARP_OPCODE_DYNAMIC_REQUEST  0x0005      ///< Dynamic ARP Request
#define ARP_OPCODE_DYNAMIC_REPLY    0x0006      ///< Dynamic ARP Reply
#define ARP_OPCODE_DYNAMIC_ERROR    0x0007      ///< Dynamic ARP Error
#define ARP_OPCODE_INVERSE_REQUEST  0x0008      ///< Inverse ARP Error
#define ARP_OPCODE_INVERSE_REPLY    0x0009      ///< Inverse ARP Reply
#define ARP_OPCODE_NAK              0x000A      ///< ARP NAK (Negative Acknowledgment)

namespace packet
{
/**
 * @struct ArpHeaderRaw
 * @brief Represents a raw ARP header.
 * @ingroup PACKET_HEADERS
 */
#pragma pack(push, 1)
struct ArpHeaderRaw
{
    uint8_t hardwareType[2];
    uint8_t protocolType[2];
    uint8_t hardwareSize;
    uint8_t protocolSize;
    uint8_t opcode[2];
    uint8_t senderHardwareAddress[6];
    uint8_t senderIpAddress[4];
    uint8_t targetHardwareAddress[6];
    uint8_t targetIpAddress[4];
};
#pragma pack(pop)

/**
 * @struct ArpHeader
 * @brief Represents an ARP (Address Resolution Protocol) header.
 * @ingroup PACKET_HEADERS
 */
struct ArpHeader
{
    DEFINE_FIXED_HEADER(ArpHeaderRaw);

    uint16_t getHardwareType() const { return utils::read<uint16_t>(raw->hardwareType); }
    uint16_t getProtocolType() const { return utils::read<uint16_t>(raw->protocolType); }
    uint8_t  getHardwareSize() const { return raw->hardwareSize; }
    uint8_t  getProtocolSize() const { return raw->protocolSize; }
    uint16_t getOpcode() const { return utils::read<uint16_t>(raw->opcode); }
    const uint8_t* getSenderIpAddr() const { return raw->senderIpAddress; }
    const uint8_t* getTargetIpAddr() const { return raw->targetIpAddress; }
    const uint8_t* getSenderHwAddr() const { return raw->senderHardwareAddress; }
    const uint8_t* getTargetHwAddr() const { return raw->targetHardwareAddress; }

    void setHardwareType(uint16_t val) 
        { utils::write<uint16_t>(raw->hardwareType, val); }
    void setProtocolType(uint16_t val)
        { utils::write<uint16_t>(raw->protocolType, val); }
    void setHardwareSize(uint8_t val)
        { raw->hardwareSize = val; }
    void setProtocolSize(uint8_t val)
        { raw->protocolSize = val; }
    void setOpcode(uint16_t val)
        { utils::write<uint16_t>(raw->opcode, val); }
    void setSenderHwAddr(uint64_t val)
        { utils::write<uint64_t, 6>(raw->senderHardwareAddress, val); }
    void setSenderIpAddr(uint32_t val)
        { utils::write<uint32_t>(raw->senderIpAddress, val); }
    void setTargetHwAddr(uint64_t val)
        { utils::write<uint64_t, 6>(raw->targetHardwareAddress, val); }
    void setTargetIpAddr(uint32_t val)
        { utils::write<uint32_t>(raw->targetIpAddress, val); }
};

} // namespace packet

#endif // ARP_HEADER_HPP

