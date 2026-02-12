// IPHeader.hpp

#ifndef IP_HEADER_HPP
#define IP_HEADER_HPP

#include <span>

#include "packet/HeaderHelpers.hpp"

#define IP_ESP    0x32U ///< IP protocol number for ESP (50)
#define IP_AH     0x32U ///< IP protocol number for AH (51)
#define IP_GRE    0x2FU ///< IP protocol number for GRE (47)
#define IP_IGMP   0x02U ///< IP protocol number for IGMP (2)
#define IP_NONE   0x3BU ///< IP protocol number for NONE (59)
#define IP_TCP    0x06U ///< IP protocol number for TCP (6)
#define IP_UDP    0x11U ///< IP protocol number for UDP (11)
#define IP_ICMPV4 0x01U ///< IP protocol number for ICMPv4 (1)
#define IP_ICMPV6 0x3AU ///< IP protocol number for ICMPv6 (58)
#define IP_SCTP   0x84U ///< IP protocol number for SCTP (132)
#define IP_EIGRP  0x58U ///< IP protocol number for EIGRP (88)
#define IP_OSPF   0x59U ///< IP protocol number for OSPF (89)
#define IP_PIM    0x67U ///< IP protocol number for PIM (103)
#define IP_RSVP   0x2EU ///< IP protocol number for RSVP (46)
#define IP_IPV4   0x04U ///< IP protocol number for IPv4 (4)
#define IP_IPV6   0x29U ///< IP Protocol number for IPv6 (41)

inline constexpr uint8_t IPV4_BROADCAST[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; ///< Broadcast IPv4 address (255.255.255.255).
inline constexpr uint8_t IPV4_SOURCE[4] = { 0x00, 0x00, 0x00, 0x00 };    ///< Placeholder source IPv4 address (0.0.0.0).
inline constexpr uint8_t IPV6_MULTICAST[16] = {0xFF, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
inline constexpr uint8_t IPV6_SOURCE[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * @struct IPv4HeaderRaw
 * @brief Represents a raw IPv4 header
 */
#pragma pack(push, 1)
struct IPv4HeaderRaw
{
    uint8_t versionAndLength;
    uint8_t typeOfService;
    uint8_t totalLength[2];
    uint8_t identification[2];
    uint8_t fragmentFlags[2];
    uint8_t ttl;
    uint8_t protocol;
    uint8_t checksum[2];
    uint8_t sourceAddress[4];
    uint8_t destinationAddress[4];

};
#pragma pack(pop)

/**
 * @struct IPv4Header
 * @brief Represents an IPv4 header.
 */
struct IPv4Header
{
    DEFINE_PACKET_HEADER(IPv4HeaderRaw);

    uint8_t getVersion() const { return raw->versionAndLength >> 4; }
    uint8_t getHeaderLength() const { return raw->versionAndLength & 0x0F; }
    uint8_t getTypeOfService() const { return raw->typeOfService; }
    uint16_t getTotalLength() { return readU16(raw->totalLength); }
    uint16_t getIdentification() { return readU16(raw->identification); }
    uint8_t getFlags() { return raw->fragmentFlags[0] >> 5; }
    uint16_t getFragmentOffset() { return ((raw->fragmentFlags[0] & 0x1f) << 8) | raw->fragmentFlags[1]; }
    uint8_t getTtl() const { return raw->ttl; }
    uint8_t getProtocol() const { return raw->protocol; }
    uint16_t getHeaderChecksum() { return readU16(raw->checksum); }
    uint8_t* getSourceAddress() const { return raw->sourceAddress; }
    uint8_t* getDestinationAddress() const { return raw->destinationAddress; }

    // Setters
    void setVersion(uint8_t version) 
        { raw->versionAndLength = (version << 4) | (raw->versionAndLength & 0x0F); }
    void setHeaderLength(uint8_t ihl) 
        { raw->versionAndLength = (raw->versionAndLength & 0xF0) | (ihl & 0x0F); }
    void setTypeOfService(uint8_t val) 
        { raw->typeOfService = val; }
    void setTotalLength(uint16_t val) 
        { writeU16(raw->totalLength, val); }
    void setIdentification(uint16_t val)
        { writeU16(raw->identification, val); }
    void setFlags(bool rs, bool mf, bool df)
        { uint8_t flags = 0; 
          if (rs) { flags |= (1 << 7); }
          if (df) { flags |= (1 << 6); }
          if (mf) { flags |= (1 << 5); }
          flags |= (raw->fragmentFlags[0] & 0x1F);
          raw->fragmentFlags[0] = flags; }
    void setFragmentOffset(uint16_t offset) 
        { raw->fragmentFlags[0] = (raw->fragmentFlags[0] & 0xE0) | ((offset >> 8) & 0x1F);
          raw->fragmentFlags[1] = offset & 0xFF; }
    void setTtl(uint8_t val) 
        { raw->ttl = val; }
    void setProtocol(uint8_t val)
        { raw->protocol = val; }
    void setHeaderChecksum(uint16_t val)
        { writeU16(raw->checksum, val); }
    void setSourceAddress(const uint8_t* addr)
        { std::memcpy(raw->sourceAddress, addr, 4); }
    void setDestinationAddress(const uint8_t* addr) 
        { std::memcpy(raw->destinationAddress, addr, 4); }
};

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

#endif // IP_HEADER_HPP
