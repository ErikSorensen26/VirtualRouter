// ArpHeader.hpp

#ifndef ARP_HEADER_HPP
#define ARP_HEADER_HPP

#include <HeaderHelpers.hpp>

/**
 * @struct ArpHeaderRaw
 * @brief Represents a raw ARP header.
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
 */
struct ArpHeader
{
    DEFINE_FIXED_HEADER(ArpHeaderRaw);

    uint16_t getHardwareType() const { return readU16(raw->hardwareType); }
    uint16_t getProtocolType() const { return readU16(raw->protocolType); }
    uint8_t  getHardwareSize() const { return raw->hardwareSize; }
    uint8_t  getProtocolSize() const { return raw->protocolSize; }
    uint16_t getOpcode() const { return readU16(raw->opcode); }
    const uint8_t* getSenderIpAddr() const { return raw->senderIpAddress; }
    const uint8_t* getTargetIpAddr() const { return raw->targetIpAddress; }
    const uint8_t* getSenderHwAddr() const { return raw->senderHardwareAddress; }
    const uint8_t* getTargetHwAddr() const { return raw->targetHardwareAddress; }

    void setHardwareType(uint16_t val) 
        { writeU16(raw->hardwareType, val); }
    void setProtocolType(uint16_t val)
        { writeU16(raw->protocolType, val); }
    void setHardwareSize(uint8_t val)
        { raw->hardwareSize = val; }
    void setProtocolSize(uint8_t val)
        { raw->protocolSize = val; }
    void setOpcode(uint16_t val)
        { writeU16(raw->opcode, val); }
    void setSenderHwAddr(const uint8_t* val)
        { std::memcpy(raw->senderHardwareAddress, val, 6); }
    void setSenderIpAddr(const uint8_t* val)
        { std::memcpy(raw->senderIpAddress, val, 4); }
    void setSenderIpAddr(uint32_t val)
        { writeU32(raw->senderIpAddress, val); }
    void setTargetHwAddr(const uint8_t* val)
        { std::memcpy(raw->targetHardwareAddress, val, 6); }
    void setTargetIpAddr(const uint8_t* val)
        { std::memcpy(raw->targetIpAddress, val, 4); }
    void setTargetIpAddr(uint32_t val)
        { writeU32(raw->targetIpAddress, val); }
};

#endif // ARP_HEADER_HPP
