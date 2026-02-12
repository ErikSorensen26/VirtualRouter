// TcpHeader.hpp

#ifndef TCP_HEADER_HPP
#define TCP_HEADER_HPP

#include <vector>

#include "packet/TlvOptions.hpp"
#include "packet/HeaderHelpers.hpp"

#define TCP_OPTION_MSS              2
#define TCP_OPTION_WINDOW_SCALE     3
#define TCP_OPTION_SACK_PERMITTED   4
#define TCP_OPTION_TIMESTAMP        8
#define TCP_OPTION_SACK             5
#define TCP_OPTION_USER_TIMEOUT     28
#define TCP_OPTION_TCP_AUTH         29
#define TCP_OPTION_MULTIPATH        30

/**
 * @struct TcpHeaderRaw
 * @brief Represents the raw fixed part of a TCP header.
 */
#pragma pack(push, 1)
struct TcpHeaderRaw
{
    uint8_t sourcePort[2];
    uint8_t destinationPort[2];
    uint8_t sequenceNumber[4]; ///< Sequence number of the first data octet
    uint8_t ackNumber[4]; ///< Contains the value of the next sequence if the ACK control is set.
    uint8_t dataOffsetAndFlags1; ///< upper 4 bits are header length
    uint8_t flags; ///< TCP control flags
    uint8_t windowSize[2];
    uint8_t checksum[2];
    uint8_t urgentPointer[2];
};
#pragma pack(pop)

/**
 * @struct TcpHeader
 * @brief High-level TCP header parser/encoder.
 */
struct TcpHeader
{
    DEFINE_PACKET_HEADER(TcpHeaderRaw);

    // Accessors
    uint16_t getSourcePort() const         { return readU16(raw->sourcePort); }
    uint16_t getDestinationPort() const    { return readU16(raw->destinationPort); }
    uint32_t getSequenceNumber() const     { return readU32(raw->sequenceNumber); }
    uint32_t getAckNumber() const          { return readU32(raw->ackNumber); }

    uint8_t  getHeaderLength() const       { return ((raw->dataOffsetAndFlags1 >> 4) & 0x0F) * 4; }

    uint16_t getWindowSize() const         { return readU16(raw->windowSize); }
    const uint8_t* getChecksum() const           { return raw->checksum; }
    uint16_t getUrgentPointer() const      { return readU16(raw->urgentPointer); }

    bool getFlagNS() const                 { return raw->dataOffsetAndFlags1 & 0x01; }

    bool getFlagCWR() const                { return raw->flags & 0x80; }
    bool getFlagECE() const                { return raw->flags & 0x40; }
    bool getFlagURG() const                { return raw->flags & 0x20; }
    bool getFlagACK() const                { return raw->flags & 0x10; }
    bool getFlagPSH() const                { return raw->flags & 0x08; }
    bool getFlagRST() const                { return raw->flags & 0x04; }
    bool getFlagSYN() const                { return raw->flags & 0x02; }
    bool getFlagFIN() const                { return raw->flags & 0x01; }

    // Setters
    void setSourcePort(uint16_t val)       { writeU16(raw->sourcePort, val); }
    void setDestinationPort(uint16_t val)  { writeU16(raw->destinationPort, val); }
    void setSequenceNumber(uint32_t val)   { writeU32(raw->sequenceNumber, val); }
    void setAckNumber(uint32_t val)        { writeU32(raw->ackNumber, val); }
    void setWindowSize(uint16_t val)       { writeU16(raw->windowSize, val); }
    void setChecksum(const uint8_t* val)   { std::memcpy(raw->checksum, val, 2); }
    void setUrgentPointer(uint16_t val)    { writeU16(raw->urgentPointer, val); }

    void setHeaderLengthBytes(uint8_t bytes)
    {
        uint8_t words = (bytes / 4) & 0x0F;
        raw->dataOffsetAndFlags1 =
            static_cast<uint8_t>((raw->dataOffsetAndFlags1 & 0x0F) | (words << 4));
    }

    void setFlagNS(bool v)                 { setBit(&raw->dataOffsetAndFlags1, 0, v); }

    void setFlagCWR(bool v)                { setBit(&raw->flags, 7, v); }
    void setFlagECE(bool v)                { setBit(&raw->flags, 6, v); }
    void setFlagURG(bool v)                { setBit(&raw->flags, 5, v); }
    void setFlagACK(bool v)                { setBit(&raw->flags, 4, v); }
    void setFlagPSH(bool v)                { setBit(&raw->flags, 3, v); }
    void setFlagRST(bool v)                { setBit(&raw->flags, 2, v); }
    void setFlagSYN(bool v)                { setBit(&raw->flags, 1, v); }
    void setFlagFIN(bool v)                { setBit(&raw->flags, 0, v); }
};

/**
 * @brief Parses TCP options into TcpOption structures.
 */
inline bool parseTcpOptions(const uint8_t* data, size_t size, std::vector<TLV8Option>& outOptions)
{
    size_t offset = 0;
    while (offset < size)
    {
        uint8_t type = data[offset];
        if (type == 0) break; // End of options list
        if (type == 1) {
            outOptions.emplace_back(type, 1, nullptr, 0);
            ++offset;
            continue;
        }

        if (offset + 2 > size) return false;
        uint8_t length = data[offset + 1];
        if (length < 2 || offset + length > size) return false;

        uint8_t* value = const_cast<uint8_t*>(data) + offset + 2;
        outOptions.emplace_back(type, length, value, static_cast<size_t>(length - 2));

        offset += length;
    }
    return true;
}

#endif // TCP_HEADER_HPP
