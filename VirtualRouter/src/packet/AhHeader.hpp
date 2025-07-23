// AhHeader.hpp

#ifndef AH_HEADER_HPP
#define AH_HEADER_HPP

#include <span>
#include <HeaderHelpers.hpp>

/**
 * @struct AhHeader
 * @brief Represents an AH (Authentication Header) header.
 */
#pragma pack(push, 1)
struct AhHeaderRaw
{
    uint8_t nextHeader[1];      ///< Next Header field.
    uint8_t payloadLength[1];    ///< Payload Length field.
    uint8_t reserved[2];  ///< Reserved field.
    uint8_t spi[4];       ///< Security Parameters Index (SPI).
    uint8_t sequence[4];  ///< Sequence Number.
};
#pragma pack(pop)

struct AhHeader
{
    DEFINE_PACKET_HEADER(AhHeaderRaw);

    // Accessors
    uint8_t getNextHeader() const { return raw->nextHeader[0]; }
    uint8_t getPayloadLength() const { return raw->payloadLength[0]; }
    const uint8_t* getReserved() const { return raw->reserved; }
    uint32_t getSpi() const { return readU32(raw->spi); }
    uint32_t getSequence() const { return readU32(raw->sequence); }

    void setNextHeader(uint8_t val)
        { raw->nextHeader[0] = val; }
    void setPayloadLength(uint8_t val)
        { raw->payloadLength[0] = val; }
    void setReserved(uint8_t* val)
        { std::memcpy(raw->reserved, val, 2); }
    void setSpi(uint32_t val)
        { writeU32(raw->spi, val); }
    void setSequence(uint32_t val)
        { writeU32(raw->sequence, val); }
};

#endif //AH_HEADER_HPP
