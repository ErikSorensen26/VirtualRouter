// IPv4Header.hpp

#ifndef IPV4_HEADER_HPP
#define IPV4_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <span>

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
#endif // IPV4_HEADER_HPP
