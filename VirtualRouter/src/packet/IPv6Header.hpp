// IPv6Header.hpp

#ifndef IPV6_HEADER_HPP
#define IPV6_HEADER_HPP

#include <HeaderHelpers.hpp>

/**
 * @struct IPv6HeaderRaw
 * @brief Represents a raw IPv6 header.
 */
#pragma pack(push, 1)
struct IPv6HeaderRaw
{
    uint8_t versionTrafficFlow[4];
    uint8_t payloadLength[2];
    uint8_t nextHeader;
    uint8_t hopLimit;
    uint8_t sourceAddress[16];
    uint8_t destinationAddress[16];
};
#pragma pack(pop)

/**
 * @struct IPv6Header
 * @brief Represents an IPv6 header.
 */
struct IPv6Header
{
    DEFINE_FIXED_HEADER(IPv6HeaderRaw);

    uint8_t getVersion() const 
        { return (raw->versionTrafficFlow[0] >> 4) & 0x0F; }
    uint8_t getTrafficClass() const 
        { return ((raw->versionTrafficFlow[0] & 0x0F) << 4) | (raw->versionTrafficFlow[1] >> 4); }
    uint32_t getFlowLabel() const 
        { return ((static_cast<uint32_t>(raw->versionTrafficFlow[1] & 0x0F) << 16) |
        (static_cast<uint32_t>(raw->versionTrafficFlow[2]) << 8) | raw->versionTrafficFlow[3]); }
    uint16_t getPayloadLength() const
        { return readU16(raw->payloadLength); }
    uint8_t getNextHeader() const
        { return raw->nextHeader; }
    uint8_t getHopLimit() const
        { return raw->hopLimit; }
    uint8_t* getSourceAddress() const
        { return raw->sourceAddress; }
    uint8_t* getDestinationAddress() const
        { return raw->destinationAddress; }

    // Setters
    void setVersionTrafficClassFlow(uint8_t version, uint8_t trafficClass, uint32_t flowLabel) 
        { raw->versionTrafficFlow[0] = (version << 4) | (trafficClass >> 4);
          raw->versionTrafficFlow[1] = ((trafficClass & 0x0F) << 4) | ((flowLabel >> 16) & 0x0F);
          raw->versionTrafficFlow[2] = (flowLabel >> 8) & 0xFF;
          raw->versionTrafficFlow[3] = flowLabel & 0xFF; }
    void setPayloadLength(uint16_t len)
        { writeU16(raw->payloadLength, len); }
    void setNextHeader(uint8_t val)
        { raw->nextHeader = val; }
    void setHopLimit(uint8_t val) 
        { raw->hopLimit = val; }
    void setSourceAddress(const uint8_t* addr)
        { std::memcpy(raw->sourceAddress, addr, 16); }
    void setDestinationAddress(const uint8_t* addr) 
        { std::memcpy(raw->destinationAddress, addr, 16); }
};

#endif // IPV6_HEADER_HPP
