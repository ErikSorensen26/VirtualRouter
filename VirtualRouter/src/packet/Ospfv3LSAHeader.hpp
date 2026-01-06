// Ospfv3LSAHeader.hpp

#ifndef OSPFV3_LSA_HEADER_HPP
#define OSPFV3_LSA_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

#define OSPFV3_LSA_ROUTER                  0x0001 ///< OSPFv3 Router LSA
#define OSPFV3_LSA_NETWORK                 0x0002 ///< OSPFv3 Network LSA
#define OSPFV3_LSA_INTER_AREA_PREFIX       0x0003 ///< OSPFv3 Inter-Area Prefix LSA
#define OSPFV3_LSA_INTER_AREA_ROUTER       0x0004 ///< OSPFv3 Inter-Area Router LSA
#define OSPFV3_LSA_AS_EXTERNAL             0x0005 ///< OSPFv3 AS-External LSA
#define OSPFV3_LSA_NSSA_EXTERNAL           0x0007 ///< OSPFv3 NSSA External LSA
#define OSPFV3_LSA_LINK                    0x0008 ///< OSPFv3 Link LSA
#define OSPFV3_LSA_INTRA_AREA_PREFIX       0x0009 ///< OSPFv3 Intra-Area Prefix LSA

#define OSPFV3_TYPE_ROUTER_INFORMATION     0x000C ///< Router Information LS type (RFC 7770)
#define OSPFV3_TYPE_EGRESS_PEER            0x000D ///< Egress Peer Engineering LS type (RFC 5392)
#define OSPFV3_TYPE_GRACEFUL_RESTART       0x000E ///< Graceful Restart LS type (RFC 5187)

#define OSPFV3_TYPE_EXTENDED_PREFIX        0x0010 ///< Extended Prefix LS type (RFC 8362)
#define OSPFV3_TYPE_EXTENDED_LINK          0x0011 ///< Extended Link LS type (RFC 8362)

#define OSPFV3_TYPE_SR_PREFIX              0x0012 ///< Segment Routing Prefix LS type (RFC 8665)
#define OSPFV3_TYPE_SR_LINK                0x0013 ///< Segment Routing Link LS type (RFC 8665)

#define OSPFV3_TYPE_AF_PREFIX              0x0014 ///< Address-Family Prefix LS type (RFC 5838)
#define OSPFV3_TYPE_AF_ROUTER              0x0015 ///< Address-Family Router LS type (RFC 5838)

#define OSPFV3_OPT_V6                      0x000001 ///< IPv6 forwarding capability
#define OSPFV3_OPT_E                       0x000002 ///< External routing capability
#define OSPFV3_OPT_N                       0x000004 ///< NSSA capability
#define OSPFV3_OPT_R                       0x000008 ///< Router bit
#define OSPFV3_OPT_DC                      0x000020 ///< Demand circuit support
#define OSPFV3_OPT_AF                      0x000040 ///< Address-family support
#define OSPFV3_OPT_L                       0x000080 ///< Link-local signaling support

#define OSPFV3_LINK_P2P                    0x01 ///< Point-to-point link
#define OSPFV3_LINK_TRANSIT                0x02 ///< Transit network link
#define OSPFV3_LINK_STUB                   0x03 ///< Stub network link
#define OSPFV3_LINK_VIRTUAL                0x04 ///< Virtual link

#define OSPFV3_RI_TLV_INFO_CAP             0x0001 ///< Router informational capabilities TLV
#define OSPFV3_RI_TLV_FUNC_CAP             0x0002 ///< Router functional capabilities TLV
#define OSPFV3_RI_TLV_SR_ALGO              0x0003 ///< Segment Routing Algorithm TLV
#define OSPFV3_RI_TLV_SRGB                 0x0004 ///< Segment Routing Global Block TLV
#define OSPFV3_RI_TLV_NODE_MSD             0x0005 ///< Node Maximum SID Depth TLV

#define OSPFV3_GR_TLV_GRACE_PERIOD         0x0001 ///< Graceful Restart grace period TLV
#define OSPFV3_GR_TLV_RESTART_REASON       0x0002 ///< Graceful Restart reason TLV
#define OSPFV3_GR_TLV_INTERFACE_ID         0x0003 ///< Graceful Restart interface identifier TLV

#define OSPFV3_GR_REASON_UNKNOWN           0x00 ///< Graceful Restart reason unknown
#define OSPFV3_GR_REASON_SW_RESTART        0x01 ///< Graceful Restart software restart
#define OSPFV3_GR_REASON_SW_UPGRADE        0x02 ///< Graceful Restart software upgrade
#define OSPFV3_GR_REASON_CP_RESTART        0x03 ///< Graceful Restart control-plane restart

#define OSPFV3_EP_TLV_PREFIX               0x0001 ///< Extended Prefix TLV

#define OSPFV3_EP_SUBTLV_PREFIX_SID        0x0001 ///< Extended Prefix Prefix-SID sub-TLV
#define OSPFV3_EP_SUBTLV_PREFIX_ATTR       0x0002 ///< Extended Prefix attributes sub-TLV

#define OSPFV3_EL_TLV_LINK                 0x0001 ///< Extended Link TLV

#define OSPFV3_EL_SUBTLV_ADJ_SID           0x0001 ///< Extended Link Adjacency SID sub-TLV
#define OSPFV3_EL_SUBTLV_LAN_ADJ_SID       0x0002 ///< Extended Link LAN Adjacency SID sub-TLV
#define OSPFV3_EL_SUBTLV_LINK_MSD          0x0003 ///< Extended Link Maximum SID Depth sub-TLV

#define OSPFV3_LSA_MAX_AGE                 3600 ///< OSPFv3 MaxAge in seconds

/*
 * @struct Ospfv3LSAHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv3LSAHeaderRaw
{
    uint8_t age[2];
    uint8_t type[2];
    uint8_t lsID[4];
    uint8_t advRouter[4];
    uint8_t seqNum[4];
    uint8_t checksum[2];
    uint8_t length[2];
};
#pragma pack(pop)

/*
 * @struct Ospfv3LSAHeader
 * @brief Represents an OSPF (Open Shortest Path First) link state header.
 */
struct Ospfv3LSAHeader
{
    DEFINE_PACKET_HEADER(Ospfv3LSAHeaderRaw);

    uint16_t getAge() const                 { return readU16(raw->age); }
    uint16_t getType() const                { return readU16(raw->type); }
    uint32_t getLsId() const                { return readU32(raw->lsID); }
    uint32_t getAdvRouter() const           { return readU32(raw->advRouter); }
    uint32_t getSeqNumber() const           { return readU32(raw->seqNum); }
    uint16_t getChecksum() const            { return readU16(raw->checksum); }
    uint16_t getLen() const                 { return readU16(raw->length); }
    
    bool getFlagB(uint8_t flags) const      { return flags & 0x01; }
    bool getFlagE(uint8_t flags) const      { return flags & 0x02; }
    bool getFlagV(uint8_t flags) const      { return flags & 0x04; }
    bool getFlagW(uint8_t flags) const      { return flags & 0x08; }
    bool getFlagN(uint8_t flags) const      { return flags & 0x10; }
    bool getFlagS(uint8_t flags) const      { return flags & 0x20; }
    bool getFlagH(uint8_t flags) const      { return flags & 0x80; }

    void setAge(uint16_t val)
        { writeU16(raw->age, val); }
    void setType(uint16_t val)
        { writeU16(raw->type, val); }
    void setLsID(uint32_t val)
        { writeU32(raw->lsID, val); }
    void setAdvRouter(uint32_t val)
        { writeU32(raw->advRouter, val); }
    void setSeqNum(uint32_t val)
        { writeU32(raw->seqNum, val); }
    void setLen(uint16_t val)
        { writeU16(raw->length, val); }

    void setFlagB(bool val, uint8_t* flags)
        { setBit(flags, 7, val); }
    void setFlagE(bool val, uint8_t* flags)
        { setBit(flags, 6, val); }
    void setFlagV(bool val, uint8_t* flags)
        { setBit(flags, 5, val); }
    void setFlagW(bool val, uint8_t* flags)
        { setBit(flags, 4, val); }
    void setFlagN(bool val, uint8_t* flags)
        { setBit(flags, 3, val); }
    void setFlagS(bool val, uint8_t* flags)
        { setBit(flags, 2, val); }
    void setFlagH(bool val, uint8_t* flags)
        { setBit(flags, 0, val); }
};

#endif // OSPFV3_LSA_HEADER_HPP
