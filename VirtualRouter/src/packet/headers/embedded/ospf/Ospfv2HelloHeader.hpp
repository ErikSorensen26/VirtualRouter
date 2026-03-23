// Ospfv2HelloHeader

#ifndef OSPFV2_HELLO_HEADER_HPP
#define OSPFV2_HELLO_HEADER_HPP

#include "packet/HeaderHelpers.hpp"

namespace packet
{

/*
 * @struct Ospfv2HelloHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv2HelloHeaderRaw
{
    uint8_t networkMask[4];
    uint8_t helloInterval[2];
    uint8_t options;
    uint8_t routerPriority;
    uint8_t deadInterval[4];
    uint8_t designatedRouter[4];
    uint8_t backupDesignatedRouter[4];
    // Neighbor Router IDs (N * 32-bit)
};
#pragma pack(pop)

/*
 * @struct Ospfv2HelloHeader
 * @brief Represents an OSPF (Open Shortest Path First) hello header.
 */
struct Ospfv2HelloHeader
{
    DEFINE_FIXED_HEADER(Ospfv2HelloHeaderRaw);

    uint32_t getMask() const                { return utils::readU32(raw->networkMask); }
    uint16_t getHelloInterval() const       { return utils::readU16(raw->helloInterval); }
    uint8_t getPriority() const             { return raw->routerPriority; }
    uint32_t getDeadInterval() const        { return utils::readU32(raw->deadInterval); }
    uint32_t getDR() const                  { return utils::readU32(raw->designatedRouter); }
    uint32_t getBDR() const                 { return utils::readU32(raw->backupDesignatedRouter); }
    uint8_t getOptions() const              { return raw->options; }

    void setMask(uint32_t val)
        { utils::writeU32(raw->networkMask, val); }
    void setHelloInterval(uint16_t val)
        { utils::writeU16(raw->helloInterval, val); }
    void setPriority(uint8_t val)
        { raw->routerPriority = val; }
    void setDeadInterval(uint32_t val)
        { utils::writeU32(raw->deadInterval, val); }
    void setDR(uint32_t val)
        { utils::writeU32(raw->designatedRouter, val); }
    void setBDR(uint32_t val)
        { utils::writeU32(raw->backupDesignatedRouter, val); }
    void setOptions(uint8_t val)
        { raw->options = val; }
};

} // namespace packet

#endif // OSPFV2_HELLO_HEADER_HPP

