// Ospfv3Header.hpp

#ifndef OSPFV3_HEADER_HPP
#define OSPFV3_HEADER_HPP

#include <span>
#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"

#define OSPFV3_VERSION 3 ///< OSPFv2 Version (2).

#define OSPFV3_TYPE_HELLO                   0x01 ///< OSPFv3 Hello Type (1).
#define OSPFV3_TYPE_DATABASE_DESCRIPTION    0x02 ///< OSPFv3 Database Description Type (2).
#define OSPFV3_TYPE_LINK_STATE_REQUEST      0x03 ///< OSPFv3 Link State Request Type (3).
#define OSPFV3_TYPE_LINK_STATE_UPDATE       0x04 ///< OSPFv3 Link State Update Type (4).
#define OSPFV3_TYPE_LINK_STATE_ACK          0x05 ///< OSPFv3 Link State Acknowledgment (5).

static constexpr uint8_t OSPFV3_ALL_SPF_ROUTERS[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05 };
static constexpr uint8_t OSPFV3_ALL_D_ROUTERS[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06 };

namespace packet
{

/**
 * @struct Ospfv3HeaderRaw
 */
#pragma pack(push, 1) 
struct Ospfv3HeaderRaw
{
    uint8_t version;
    uint8_t type;
    uint8_t packetLength[2];
    uint8_t routerID[4];
    uint8_t areaID[4];
    uint8_t checksum[2];
    uint8_t instanceID;
    uint8_t reserved;
};
#pragma pack(pop)

/**
 * @struct Ospfv3Header
 * @brief Represents an OSPF (Open Shortest Path First) header.
 */
struct Ospfv3Header
{
    DEFINE_PACKET_HEADER(Ospfv3HeaderRaw);

    uint8_t getVersion() const              { return raw->version; }
    uint8_t getType() const                 { return raw->type; }
    uint16_t getPacketLen() const           { return utils::readU16(raw->packetLength); }
    uint32_t getRouterID() const            { return utils::readU32(raw->routerID); }
    uint32_t getAreaID() const              { return utils::readU32(raw->areaID); }
    uint16_t getChecksum() const            { return utils::readU16(raw->checksum); }
    uint8_t getInstanceID() const           { return raw->instanceID; }

    void setVersion(uint8_t val)
        { raw->version = val; }
    void setType(uint8_t val)
        { raw->type = val; }
    void setPacketLen(uint16_t val)
        { utils::writeU16(raw->packetLength, val); }
    void setRouterID(uint32_t val)
        { utils::writeU32(raw->routerID, val); }
    void setAreaID(uint32_t val)
        { utils::writeU32(raw->areaID, val); }
    void setInstanceID(uint8_t val)
        { raw->instanceID = val; }
};

} // namespace packet

#endif // OSPFV3_HEADER_HPP

