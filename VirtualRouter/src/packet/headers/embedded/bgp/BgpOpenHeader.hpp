// BgpHeader.hpp

#ifndef BGP_OPEN_HEADER_HPP
#define BGP_OPEN_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

#pragma pack(push, 1)
struct BgpOpenHeaderRaw
{
    uint8_t version;
    uint8_t asNumber[2];
    uint8_t identifier[4];
    uint8_t parameterLen;
};
#pragma pack(pop)

/**
 * @struct BgpOpenHeader
 * @brief Represents a BGP Open type header.
 */
struct BgpOpenHeader
{
    DEFINE_FIXED_HEADER(BgpOpenHeaderRaw);

    uint8_t getVersion() const { return raw->version; }
    uint16_t getAsNumber() const { return readU16(raw->asNumber); }
    uint32_t getIdentifier() const { return readU32(raw->identifier); }
    uint8_t getParameterLen() const { return raw->parameterLen; }

    void setVersion(uint8_t val)
        { raw->version = val; }
    void setAsNumber(uint16_t val)
        { writeU16(raw->asNumber, val); }
    void setIdentifier(uint32_t val)
        { writeU32(raw->identifier, val); }
    void setParameterLen(uint8_t val)
        { raw->parameterLen = val; }
};

#endif // BGP_HEADER_HPP
