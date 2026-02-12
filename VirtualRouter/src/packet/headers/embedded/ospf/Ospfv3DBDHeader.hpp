// Ospfv3DBDHeader.hpp

#ifndef OSPFV3_DBD_HEADER_HPP
#define OSPFV3_DBD_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

/*
 * @struct Ospfv3DBDHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv3DBDHeaderRaw
{
    uint8_t reserved;
    uint8_t options[3];
    uint8_t mtu[3];
    uint8_t flags;
    uint8_t seqNum[4];
    // LSA headers
};
#pragma pack(pop)

/*
 * @struct Ospfv3DBDHeader
 * @brief Represents an OSPF (Open Shortest Path First) database header.
 */
struct Ospfv3DBDHeader
{
    DEFINE_FIXED_HEADER(Ospfv3DBDHeaderRaw);

    uint16_t getMtu() const                 { return readU16(raw->mtu); }
    uint32_t getOptions() const             { return readU16(raw->mtu); }
    uint32_t getSeqNum() const              { return readU32(raw->seqNum); }

    bool getFlagMS() const                  { return raw->flags & 0x01; }
    bool getFlagM() const                   { return raw->flags & 0x02; }
    bool getFlagI() const                   { return raw->flags & 0x04; }
    bool getFlagR() const                   { return raw->flags & 0x08; }

    void setMtu(uint16_t val)
        { writeU16(raw->mtu, val); }
    void setSequence(uint32_t val)
        { writeU32(raw->seqNum, val); }

    void setFlagMS(bool val)
        { setBit(&raw->flags, 7, val); }
    void setFlagM(bool val)
        { setBit(&raw->flags, 6, val); }
    void setFlagI(bool val)
        { setBit(&raw->flags, 5, val); }
    void setFlagR(bool val)
        { setBit(&raw->flags, 4, val); }
};

#endif // OSPFV3_DBD_HEADER_HPP
