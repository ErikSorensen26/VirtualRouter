/**
 * @file Ospfv2DBDHeader.hpp
 */

// Ospfv2DBDHeader.hpp

#ifndef OSPFV2_DBD_HEADER_HPP
#define OSPFV2_DBD_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

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

    uint16_t getMtu() const                 { return utils::read<uint16_t>(raw->mtu); }
    uint32_t getSequence() const            { return utils::read<uint32_t>(raw->sequence); }
    uint8_t getOptions() const              { return raw->options; }
    uint8_t getFlags() const                { return raw->flags; }

    bool getFlagMS() const                  { return raw->flags & 0x01; }
    bool getFlagM() const                   { return raw->flags & 0x02; }
    bool getFlagI() const                   { return raw->flags & 0x04; }
    bool getFlagR() const                   { return raw->flags & 0x08; }

    void setMtu(uint16_t val)
        { utils::write<uint16_t>(raw->mtu, val); }
    void setSequence(uint32_t val)
        { utils::write<uint32_t>(raw->sequence, val); }
    void setOptions(uint8_t val)
        { raw->options = val; }

    void setFlagMS(bool val)
        { utils::setBit(&raw->flags, 7, val); }
    void setFlagM(bool val)
        { utils::setBit(&raw->flags, 6, val); }
    void setFlagI(bool val)
        { utils::setBit(&raw->flags, 5, val); }
    void setFlagR(bool val)
        { utils::setBit(&raw->flags, 4, val); }
};

} // namespace packet

#endif // OSPFV2_DBD_HEADER_HPP

