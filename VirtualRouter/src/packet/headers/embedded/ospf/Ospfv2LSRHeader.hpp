/**
 * @file Ospfv2LSRHeader.hpp
 */

// Ospfv2LSRHeader.hpp

#ifndef OSPFV2_LSR_HEADER_HPP
#define OSPFV2_LSR_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/*
 * @struct Ospfv2LSRHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv2LSRHeaderRaw
{
    uint8_t type[4];
    uint8_t lsID[4];
    uint8_t advRouter[4];
};
#pragma pack(pop)

/*
 * @struct Ospfv2LSRHeader
 * @brief Represents an OSPF (Open Shortest Path First) link state header.
 */
struct Ospfv2LSRHeader
{
    DEFINE_FIXED_HEADER(Ospfv2LSRHeaderRaw);

    uint32_t getType() const           { return utils::read<uint32_t>(raw->type); }
    uint32_t getLsID() const                { return utils::read<uint32_t>(raw->lsID); }
    uint32_t getAdvRouter() const           { return utils::read<uint32_t>(raw->advRouter); }

    void setType(uint32_t val)
        { utils::write<uint32_t>(raw->type, val); }
    void setLsID(uint32_t val)
        { utils::write<uint32_t>(raw->lsID, val); }
    void setAdvRouter(uint32_t val)
        { utils::write<uint32_t>(raw->advRouter, val); }
};

} // namespace packet

#endif // OSPFV2_LSR_HEADER_HPP

