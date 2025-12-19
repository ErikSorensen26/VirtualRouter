// Ospfv2DBDHeader.hpp

#ifndef OSPFV2_DBD_HEADER_HPP
#define OSPFV2_DBD_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

/*
 * @struct Ospfv2DBDHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv2DBDHeaderRaw
{
    uint8_t mtu[2];
    uint8_t options;
    uint8_t flags;
    uint8_t sequence[4];
    // LSA headers
};
#pragma pack(pop)

/*
 * @struct Ospfv2DBDHeader
 * @brief Represents an OSPF (Open Shortest Path First) database header.
 */
struct Ospfv2DBDHeader
{
    DEFINE_FIXED_HEADER(Ospfv2DBDHeaderRaw);

    uint16_t getMtu() const                 { return readU16(raw->mtu); }
    uint32_t getSequence() const            { return readU32(raw->sequence); }

    bool getOptMT() const                   { return raw->options & 0x01; }
    bool getOptE() const                    { return raw->options & 0x02; }
    bool getOptMC() const                   { return raw->options & 0x04; }
    bool getOptNP() const                   { return raw->options & 0x08; }
    bool getOptEA() const                   { return raw->options & 0x10; }
    bool getOptDC() const                   { return raw->options & 0x20; }
    bool getOptO() const                    { return raw->options & 0x40; }

    bool getFlagMS() const                  { return raw->flags & 0x01; }
    bool getFlagM() const                   { return raw->flags & 0x02; }
    bool getFlagI() const                   { return raw->flags & 0x04; }
    bool getFlagR() const                   { return raw->flags & 0x08; }

    void setMtu(uint16_t val)
        { writeU16(raw->mtu, val); }
    void setSequence(uint32_t val)
        { writeU32(raw->sequence, val); }

    void setOptMT(bool val)
        { setBit(&raw->options, 7, val); }
    void setOptE(bool val)
        { setBit(&raw->options, 6, val); }
    void setOptMC(bool val)
        { setBit(&raw->options, 5, val); }
    void setOptNP(bool val)
        { setBit(&raw->options, 4, val); }
    void setOptEA(bool val)
        { setBit(&raw->options, 3, val); }
    void setOptDC(bool val)
        { setBit(&raw->options, 2, val); }
    void setOptO(bool val)
        { setBit(&raw->options, 1, val); }

    void setFlagMS(bool val)
        { setBit(&raw->flags, 7, val); }
    void setFlagM(bool val)
        { setBit(&raw->flags, 6, val); }
    void setFlagI(bool val)
        { setBit(&raw->flags, 5, val); }
    void setFlagR(bool val)
        { setBit(&raw->flags, 4, val); }
};

#endif // OSPFV2_DBD_HEADER_HPP
