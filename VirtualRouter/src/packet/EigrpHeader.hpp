// EigrpHeader.hpp

#ifndef EIGRP_HEADER_HPP
#define EIGRP_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TLVOptions.hpp>
#include <vector>

/**
 * @struct EigrpHeaderRaw
 */
#pragma pack(push, 1) 
struct EigrpHeaderRaw
{
    uint8_t version;
    uint8_t opcode;
    uint8_t checksum[2];
    uint8_t flags[4];
    uint8_t sequence[4];
    uint8_t ack[4];
    uint8_t virtualRouterId[2];
    uint8_t autonomousSystem[2];
};
#pragma pack(pop)

/**
 * @struct EigrpHeader
 * @brief Represents an EIGRP (Enhanced Interior Gateway Routing Protocol) header.
 */
struct EigrpHeader
{
    DEFINE_PACKET_HEADER(EigrpHeaderRaw);

    uint8_t getOpcode() const              { return raw->opcode; }
    uint32_t getSequence() const           { return readU32(raw->sequence); }
    uint32_t getAck() const                { return readU32(raw->ack); }
    uint16_t getVirtualRouterID() const    { return readU16(raw->virtualRouterId); }
    uint16_t getAutonomousSystem() const   { return readU16(raw->autonomousSystem); }

    bool getFlagInit() const               { return raw->flags[3] & 0x01; }
    bool getFlagCondRecv() const           { return raw->flags[3] & 0x02; }
    bool getFlagRestart() const            { return raw->flags[3] & 0x04; }
    bool getFlagEndOfTable() const         { return raw->flags[3] & 0x08; }

    void setOpcode(uint8_t val) 
        { raw->opcode = val; }
    void setSequence(uint32_t val) 
        { writeU32(raw->sequence, val); }
    void setAck(uint32_t val)
        { writeU32(raw->ack, val); }
    void setVirtualRouterId(uint32_t val)
        { writeU32(raw->ack, val); }
    void setAutonomousSystem(uint32_t val)
        { writeU32(raw->autonomousSystem, val); }

    void setFlagInit(bool val)
        { setBit(raw->flags, 31, val); }
    void setFlagCondRecv(bool val)
        { setBit(raw->flags, 30, val); }
    void setFlagRestart(bool val)
        { setBit(raw->flags, 29, val); }
    void setFlagEndOfTable(bool val)
        { setBit(raw->flags, 28, val); }
};

inline bool parseEigrpOptions(const uint8_t* data, size_t size, std::vector<TLV16Option>& outOptions)
{
    size_t offset = 0;
    while (offset + 4 <= size)
    {
        const uint16_t type = readU16(data + offset);
        const uint16_t length = readU16(data + offset + 2);
        if (length < 4 || offset + length > size) return false;

        const uint8_t* value = data + offset + 4;
        size_t valueSize = length - 4;

        outOptions.emplace_back(type, length, value, valueSize);
        
        offset += length;
    }
    return offset == size;
}

#endif // EIGRP_HEADER_HPP
