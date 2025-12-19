// Ospfv3HelloHeader.hpp

#ifndef OSPFV3_HELLO_HEADER_HPP
#define OSPFV3_HELLO_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

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

    uint32_t getInterfaceID() const         { return readU32(raw->interfaceID); }
    uint8_t getRouterPriority() const       { return raw->routerPriority; }
    uint16_t getHelloInterval() const       { return readU16(raw->helloInterval); }
    uint16_t getDeadInterval() const        { return readU16(raw->deadInterval); }
    uint32_t getDrID() const                { return readU32(raw->designatedRouterID); }
    uint32_t getBdrID() const               { return readU32(raw->backupDesignatedRouterID); }

    bool getOptV6() const                   { return raw->options[2] & 0x01; }
    bool getOptE() const                    { return raw->options[2] & 0x02; }
    bool getOptMC() const                   { return raw->options[2] & 0x04; }
    bool getOptNP() const                   { return raw->options[2] & 0x08; }
    bool getOptR() const                    { return raw->options[2] & 0x10; }
    bool getOptDC() const                   { return raw->options[2] & 0x20; }
    bool getOptAF() const                   { return raw->options[1] & 0x01; }
    bool getOptL() const                    { return raw->options[1] & 0x02; }
    bool getOptAT() const                   { return raw->options[1] & 0x04; }

    void setInterfaceID(uint32_t val)
        { writeU32(raw->interfaceID, val); }
    void setRouterPriority(uint8_t val)
        { raw->routerPriority = val; }
    void setHelloInterval(uint16_t val)
        { writeU16(raw->helloInterval, val); }
    void setDeadInterval(uint16_t val)
        { writeU32(raw->deadInterval, val); }
    void setDrID(uint32_t val)
        { writeU32(raw->designatedRouterID, val); }
    void setBdrID(uint32_t val)
        { writeU32(raw->backupDesignatedRouterID, val); }

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
};

#endif // OSPFV3_HELLO_HEADER_HPP
