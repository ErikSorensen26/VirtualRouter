// Ospfv2Header.hpp

#ifndef OSPFV2_HEADER_HPP
#define OSPFV2_HEADER_HPP

#include <span>

#include "packet/HeaderHelpers.hpp"

#define OSPFV2_VERSION 2 ///< OSPFv2 Version (2).

#define OSPFV2_TYPE_HELLO                   0x01 ///< OSPFv2 Hello Type (1).
#define OSPFV2_TYPE_DATABASE_DESCRIPTION    0x02 ///< OSPFv2 Database Description Type (2).
#define OSPFV2_TYPE_LINK_STATE_REQUEST      0x03 ///< OSPFv2 Link State Request Type (3).
#define OSPFV2_TYPE_LINK_STATE_UPDATE       0x04 ///< OSPFv2 Link State Update Type (4).
#define OSPFV2_TYPE_LINK_STATE_ACK          0x05 ///< OSPFv2 Link State Acknowledgment (5).

#define OSPFV2_AUTH_NULL    0x0000 ///< OSPFv2 Auth Type Null (0)
#define OSPFV2_AUTH_SIMPLE  0x0001 ///< OSPFv2 Auth Type Simple Password (1).
#define OSPFV2_AUTH_CRYPTO  0x0002 ///< OSPFv2 Auth Type Cryptographicc (MD5/SHA) (2).

constexpr uint8_t OSPFV2_ALL_SPF_ROUTERS[4] = { 0xE0, 0x00, 0x00, 0x05 };
constexpr uint8_t OSPFV2_ALL_D_ROUTERS[4] = { 0x0E, 0x00, 0x00, 0x06 };

/**
 * @struct Ospfv2HeaderRaw
 */
#pragma pack(push, 1) 
struct Ospfv2HeaderRaw
{
    uint8_t version;
    uint8_t type;
    uint8_t packetLength[2];
    uint8_t routerID[4];
    uint8_t areaID[4];
    uint8_t checksum[2];
    uint8_t authType[2];
    uint8_t authentication[8];
};
#pragma pack(pop)

/**
 * @struct Ospfv2Header
 * @brief Represents an OSPF (Open Shortest Path First) header.
 */
struct Ospfv2Header
{
    DEFINE_PACKET_HEADER(Ospfv2HeaderRaw);

    uint8_t getVersion() const             { return raw->version; }
    uint8_t getType() const                { return raw->type; }
    uint16_t getPacketLen() const          { return readU16(raw->packetLength); }
    uint32_t getRouterID() const           { return readU32(raw->routerID); }
    uint32_t getAreaID() const             { return readU32(raw->areaID); }
    uint16_t getChecksum() const           { return readU16(raw->checksum); }
    uint16_t getAuthType() const           { return readU16(raw->authType); }
    uint8_t* getAuthentication() const     { return raw->authentication; }

    void setVersion(uint8_t val)
        { raw->version = val; }
    void setType(uint8_t val)
        { raw->type = val; }
    void setPacketLen(uint16_t val)
        { writeU16(raw->packetLength, val); }
    void setRouterID(uint32_t val)
        { writeU32(raw->routerID, val); }
    void setAreaID(uint32_t val)
        { writeU32(raw->areaID, val); }
    void setAuthType(uint16_t val)
        { writeU16(raw->authType, val); }
    void setAuthentication(const uint8_t* val)
        { std::memcpy(raw->authentication, val, 8); }
};

#endif // OSPFV2_HEADER_HPP
