// UdpHeader.hpp

#ifndef UDP_HEADER_HPP
#define UDP_HEADER_HPP

#include <HeaderHelpers.hpp>

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
        { return readU16(raw->sourcePort); }
    uint16_t getDestinationPort() const
        { return readU16(raw->destinationPort); }
    uint16_t getLength() const
        { return readU16(raw->length); }
    const uint8_t* getChecksum() const
        { return raw->checksum; }
    
    void setChecksum(uint8_t* val)
        { std::memcpy(raw->checksum, val, 2); }
    void setSourcePort(uint16_t val)
        { writeU16(raw->sourcePort, val); }
    void setDestinationPort(uint16_t val)
        { writeU16(raw->destinationPort, val); }
};

#endif // UDP_HEADER_HPP
