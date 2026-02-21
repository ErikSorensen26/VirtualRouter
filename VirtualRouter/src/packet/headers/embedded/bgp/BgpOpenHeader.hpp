// BgpHeader.hpp

#ifndef BGP_OPEN_HEADER_HPP
#define BGP_OPEN_HEADER_HPP

#include "packet/headers/DhcpHeader.hpp"

#include <span>

#include "packet/HeaderHelpers.hpp"

#pragma pack(push, 1)
struct BgpOpenHeaderRaw
{
    uint8_t version;
    uint8_t asNumber[2];
    uint8_t holdTime[2];
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
    DEFINE_PACKET_HEADER(BgpOpenHeaderRaw);

    uint8_t getVersion() const { return raw->version; }
    uint16_t getAsNumber() const { return readU16(raw->asNumber); }
    uint16_t getHoldTime() const { return readU16(raw->holdTime); }
    uint32_t getIdentifier() const { return readU32(raw->identifier); }
    const uint8_t* getIdentifierBuf() const { return raw->identifier; }
    uint8_t getParameterLen() const { return raw->parameterLen; }

    void setVersion(uint8_t val)
        { raw->version = val; }
    void setAsNumber(uint16_t val)
        { writeU16(raw->asNumber, val); }
    void setHoldTime(uint16_t val)
        { writeU16(raw->holdTime, val); }
    void setIdentifier(uint32_t val)
        { writeU32(raw->identifier, val); }
    void setParameterLen(uint8_t val)
        { raw->parameterLen = val; }
};

inline bool parseBgpOpenParameters(const uint8_t* data, size_t size, std::vector<TLV8Option>& outOptions)
{
    size_t offset = 0;
    while (offset + 2 <= size)
    {
        const uint8_t type = data[offset];
        const uint8_t length = data[offset + 1];

        if (offset + 2 + length > size) return false;

        uint8_t* value = const_cast<uint8_t*>(data) + offset + 2;
        outOptions.emplace_back(type, length, value, length);
        
        offset += 2 + length;
    }
    return offset == size;
}

#endif // BGP_HEADER_HPP
