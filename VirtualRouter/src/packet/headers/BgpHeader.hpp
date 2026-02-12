// BgpHeader.hpp

#ifndef BGP_HEADER_HPP
#define BGP_HEADER_HPP

#include <span>

#include "packet/HeaderHelpers.hpp"

#define BGP_TYPE_OPEN 0x01
#define BGP_TYPE_UPDATE 0x02
#define BGP_TYPE_NOTIFICATION 0x03
#define BGP_TYPE_KEEP_ALIVE 0x04
#define BGP_TYPE_ROUTE_REFRESH

#pragma pack(push, 1)
struct BgpHeaderRaw
{
    uint8_t marker[16];
    uint8_t length[2];
    uint8_t type;
};
#pragma pack(pop)

/**
 * @struct BgpHeader
 * @brief Represents a BGP (Border Gateway Protocol) header.
 */
struct BgpHeader
{
    DEFINE_PACKET_HEADER(BgpHeaderRaw);

    uint16_t getLength() const { return readU16(raw->length); }
    uint8_t getType() const { return raw->type; }

    void setMarker()
        { std::memset(raw->marker, 0xff, sizeof(raw->marker)); }
    void setLength(uint16_t val)
        { writeU16(raw->length, val); }
    void setType(uint8_t val)
        { raw->type = val; }
};

#endif // BGP_HEADER_HPP
