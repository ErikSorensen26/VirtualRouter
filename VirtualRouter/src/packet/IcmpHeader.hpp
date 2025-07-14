// Icmpv6Header

#ifndef ICMP_HEADER_HPP
#define ICMP_HEADER_HPP

#include <HeaderHelpers.hpp>

/**
 * @struct IcmpHeaderRaw
 * @brief Represents a raw ICMP header.
 */
#pragma pack(push, 0)
struct IcmpHeaderRaw
{
    uint8_t type;
    uint8_t code;
    uint8_t checksum[2];
    uint8_t identifier[2];
    uint8_t sequenceNumber[2];
};
#pragma pack(pop)


/**
 * @struct IcmpHeader
 * @brief Represents an ICMP (Internet Control Message Protocol) header.
 */
struct IcmpHeader
{   
    DEFINE_FIXED_HEADER(IcmpHeaderRaw);

    // Accessors
    uint8_t getType() const 
        { return raw->type; }
    uint8_t getCode() const
        { return raw->code; }
    const uint8_t* getChecksum() const
        { return raw->checksum; }
    uint16_t getIdentifier() const
        { return readU16(raw->identifier); }
    uint16_t getSequenceNumber() const
        { return readU16(raw->sequenceNumber); }

    // Setters
    void setType(uint8_t val)
        { raw->type = val; }
    void setCode(uint8_t val)
        { raw->code = val; }
    void setChecksum(uint16_t val)
        { writeU16(raw->checksum, val); }
    void setIdentifier(uint16_t val)
        { writeU16(raw->identifier, val); }
    void setSequenceNumber(uint16_t val)
        { writeU16(raw->sequenceNumber, val); }
};

#endif // ICMP_HEADER_HPP
