/**
 * @file Ospfv3HelloHeader.hpp
 * @ingroup PACKET_HEADERS_OSPF
 */

// Ospfv3HelloHeader.hpp

#ifndef OSPFV3_HELLO_HEADER_HPP
#define OSPFV3_HELLO_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/**
 * @struct Ospfv3HelloHeaderRaw
 */
#pragma pack(push, 1) 
struct Ospfv3HelloHeaderRaw
{
    uint8_t interfaceID[4];
    uint8_t routerPriority;
    uint8_t options[3];
    uint8_t helloInterval[2];
    uint8_t deadInterval[2];
    uint8_t designatedRouterID[4];
    uint8_t backupDesignatedRouterID[4];
};
#pragma pack(pop)

/**
 * @struct Ospfv3HelloHeader
 * @brief Represents an OSPF (Open Shortest Path First) hello header.
 */
struct Ospfv3HelloHeader
{
    DEFINE_FIXED_HEADER(Ospfv3HelloHeaderRaw);

    uint32_t getInterfaceID() const         { return utils::read<uint32_t>(raw->interfaceID); }
    uint8_t getRouterPriority() const       { return raw->routerPriority; }
    uint32_t getOptions() const             { return utils::read<uint32_t, 3>(raw->options); }
    uint16_t getHelloInterval() const       { return utils::read<uint16_t>(raw->helloInterval); }
    uint16_t getDeadInterval() const        { return utils::read<uint16_t>(raw->deadInterval); }
    uint32_t getDrID() const                { return utils::read<uint32_t>(raw->designatedRouterID); }
    uint32_t getBdrID() const               { return utils::read<uint32_t>(raw->backupDesignatedRouterID); }

    void setInterfaceID(uint32_t val)
        { utils::write<uint32_t>(raw->interfaceID, val); }
    void setRouterPriority(uint8_t val)
        { raw->routerPriority = val; }
    void setOptions(uint32_t val)
        { utils::write<uint32_t, 3>(raw->options, val); }
    void setHelloInterval(uint16_t val)
        { utils::write<uint16_t>(raw->helloInterval, val); }
    void setDeadInterval(uint16_t val)
        { utils::write<uint16_t>(raw->deadInterval, val); }
    void setDrID(uint32_t val)
        { utils::write<uint32_t>(raw->designatedRouterID, val); }
    void setBdrID(uint32_t val)
        { utils::write<uint32_t>(raw->backupDesignatedRouterID, val); }
};

} // namespace packet

#endif // OSPFV3_HELLO_HEADER_HPP

