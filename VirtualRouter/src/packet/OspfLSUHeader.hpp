// Ospfv2LSUHeader.hpp

#ifndef OSPF_LSU_HEADER_HPP
#define OSPF_LSU_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

/*
 * @struct Ospfv2LSUHeaderRaw
 */
#pragma pack(push, 1)
struct OspfLSUHeaderRaw
{
    uint8_t lsas[4];
    // LSAs
};
#pragma pack(pop)

/*
 * @struct OspfLSUHeader
 * @brief Represents an OSPF (Open Shortest Path First) link state update header.
 */
struct OspfLSUHeader
{
    DEFINE_FIXED_HEADER(OspfLSUHeaderRaw);

    uint32_t getLSAs() const                { return readU32(raw->lsas); }

    void setLSAs(uint32_t val) const
        { writeU32(raw->lsas, val); }
};

#endif // OSPF_LSU_HEADER_HPP
