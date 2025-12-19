// Ospfv3DBDHeader.hpp

#ifndef OSPFV3_DBD_HEADER_HPP
#define OSPFV3_DBD_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

/*
 * @struct Ospfv3DBDHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv3DBDHeaderRaw
{
    uint8_t mtu[2];
    uint8_t options[3];
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
    uint32_t getSeqNum() const              { return readU32(raw->seqNum); }

    bool getOptV6() const                   { return raw->options[2] & 0x01; }
    bool getOptE() const                    { return raw->options[2] & 0x02; }
    bool getOptMC() const                   { return raw->options[2] & 0x04; }
    bool getOptNP() const                   { return raw->options[2] & 0x08; }
    bool getOptR() const                    { return raw->options[2] & 0x10; }
    bool getOptDC() const                   { return raw->options[2] & 0x20; }
    bool getOptAF() const                   { return raw->options[1] & 0x01; }
    bool getOptL() const                    { return raw->options[1] & 0x02; }
    bool getOptAT() const                   { return raw->options[1] & 0x04; }

    bool getFlagMS() const                  { return raw->flags & 0x01; }
    bool getFlagM() const                   { return raw->flags & 0x02; }
    bool getFlagI() const                   { return raw->flags & 0x04; }
    bool getFlagR() const                   { return raw->flags & 0x08; }

    void setMtu(uint16_t val)
        { writeU16(raw->mtu, val); }
    void setSequence(uint32_t val)
        { writeU32(raw->seqNum, val); }

    void setOptV6(bool val)
        { setBit(raw->options, 23, val); }
    void setOptE(bool val)
        { setBit(raw->options, 22, val); }
    void setOptMC(bool val)
        { setBit(raw->options, 21, val); }
    void setOptNP(bool val)
        { setBit(raw->options, 20, val); }
    void setOptR(bool val)
        { setBit(raw->options, 19, val); }
    void setOptDC(bool val)
        { setBit(raw->options, 18, val); }
    void setOptAF(bool val)
        { setBit(raw->options, 15, val); }
    void setOptL(bool val)
        { setBit(raw->options, 14, val); }
    void setOptAT(bool val)
        { setBit(raw->options, 13, val); }

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
