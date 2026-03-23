// Ospfv3LSRHeader.hpp

#ifndef OSPFV3_LSR_HEADER_HPP
#define OSPFV3_LSR_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/*
 * @struct Ospfv3LSRHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv3LSRHeaderRaw
{
    uint8_t type[2];
    uint8_t reserved[2];
    uint8_t lsID[4];
    uint8_t advRouter[4];
};
#pragma pack(pop)

/*
 * @struct Ospfv3LSRHeader
 * @brief Represents an OSPF (Open Shortest Path First) link state header.
 */
struct Ospfv3LSRHeader
{
    DEFINE_FIXED_HEADER(Ospfv3LSRHeaderRaw);

    uint16_t getType() const           { return utils::readU16(raw->type); }
    uint32_t getLsID() const                { return utils::readU32(raw->lsID); }
    uint32_t getAdvRouter() const           { return utils::readU32(raw->advRouter); }

    void setType(uint16_t val)
        { utils::writeU16(raw->type, val); }
    void setLsID(uint32_t val)
        { utils::writeU32(raw->lsID, val); }
    void setAdvRouter(uint32_t val)
        { utils::writeU32(raw->advRouter, val); }
};

} // namespace packet

#endif // OSPFV3_LSR_HEADER_HPP

