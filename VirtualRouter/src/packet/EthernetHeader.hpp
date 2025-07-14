// EthernetHeader.hpp

#ifndef ETHERNET_HEADER_HPP
#define ETHERNET_HEADER_HPP

#include <HeaderHelpers.hpp>

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

    void setSourceMac(const uint8_t* val) 
        { std::memcpy(raw->sourceMac, val, 6); }
    void setDestinationMac(const uint8_t* val)
        { std::memcpy(raw->sourceMac, val, 6); }
    void setType(const uint16_t val)
        { writeU16(raw->type, val); }
};

#endif // ETHERNET_HEADER_HPP
