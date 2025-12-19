// Ospfv2LSRHeader.hpp

#ifndef OSPFV2_LSR_HEADER_HPP
#define OSPFV2_LSR_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

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

    uint32_t getType() const           { return readU32(raw->type); }
    uint32_t getLsID() const                { return readU32(raw->lsID); }
    uint32_t getAdvRouter() const           { return readU32(raw->advRouter); }

    void setType(uint32_t val)
        { writeU32(raw->type, val); }
    void setLsID(uint32_t val)
        { writeU32(raw->lsID, val); }
    void setAdvRouter(uint32_t val)
        { writeU32(raw->advRouter, val); }
};

#endif // OSPFV2_LSR_HEADER_HPP
