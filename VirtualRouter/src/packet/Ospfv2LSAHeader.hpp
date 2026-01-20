// Ospfv2LSAHeader.hpp

#ifndef OSPFV2_LSA_HEADER_HPP
#define OSPFV2_LSA_HEADER_HPP

#include <HeaderHelpers.hpp>
#include <TlvOptions.hpp>

#define OSPFV2_LSA_ROUTER        0x01  ///< Router LSA (intra-area topology)
#define OSPFV2_LSA_NETWORK       0x02  ///< Network LSA (DR-generated)
#define OSPFV2_LSA_SUM_NET       0x03  ///< Summary LSA (inter-area network)
#define OSPFV2_LSA_SUM_ASBR      0x04  ///< Summary LSA (inter-area ASBR)
#define OSPFV2_LSA_EXTERNAL      0x05  ///< AS-External LSA
#define OSPFV2_LSA_GROUP         0x06  ///< Group Membership LSA (MOSPF, obsolete)
#define OSPFV2_LSA_NSSA          0x07  ///< NSSA External LSA
#define OSPFV2_LSA_EXT_ATTR      0x08  ///< External Attributes LSA (obsolete)
#define OSPFV2_LSA_OPAQUE_LINK   0x09  ///< Opaque LSA (link-local scope)
#define OSPFV2_LSA_OPAQUE_AREA   0x0A  ///< Opaque LSA (area scope)
#define OSPFV2_LSA_OPAQUE_AS     0x0B  ///< Opaque LSA (AS scope)

#define OSPFV2_LINK_P2P     0x01  ///< Point-to-point link
#define OSPFV2_LINK_TRANSIT 0x02  ///< Transit (multi-access) network
#define OSPFV2_LINK_STUB    0x03  ///< Stub network
#define OSPFV2_LINK_VIRTUAL 0x04  ///< Virtual link

#define OSPFV2_OPQ_ROUTER_INFO  0x01  ///< Router Information LSA
#define OSPFV2_OPQ_TE           0x02  ///< Traffic Engineering LSA
#define OSPFV2_OPQ_GR           0x03  ///< Graceful Restart LSA
#define OSPFV2_OPQ_EXT_PREFIX   0x04  ///< Extended Prefix LSA
#define OSPFV2_OPQ_EXT_LINK     0x05  ///< Extended Link LSA

#define OSPFV2_RI_TLV_INFO_CAP  0x0001  ///< Informational capabilities
#define OSPFV2_RI_TLV_FUNC_CAP  0x0002  ///< Functional capabilities
#define OSPFV2_RI_TLV_SR_ALGO   0x0003  ///< Segment Routing algorithms
#define OSPFV2_RI_TLV_SRGB      0x0004  ///< Segment Routing Global Block
#define OSPFV2_RI_TLV_NODE_MSD  0x0005  ///< Node Maximum SID Depth

#define OSPFV2_TE_TLV_ROUTER_ID 0x0001  ///< Router Address
#define OSPFV2_TE_TLV_LINK      0x0002  ///< Link TLV

#define OSPFV2_TE_SUB_LINK_TYPE   0x0001  ///< Link type
#define OSPFV2_TE_SUB_LINK_ID     0x0002  ///< Link ID
#define OSPFV2_TE_SUB_LOCAL_IP    0x0003  ///< Local interface IP
#define OSPFV2_TE_SUB_REMOTE_IP   0x0004  ///< Remote interface IP
#define OSPFV2_TE_SUB_METRIC      0x0005  ///< TE metric
#define OSPFV2_TE_SUB_MAX_BW      0x0006  ///< Maximum bandwidth
#define OSPFV2_TE_SUB_MAX_RES_BW  0x0007  ///< Max reservable bandwidth
#define OSPFV2_TE_SUB_UNRES_BW    0x0008  ///< Unreserved bandwidth
#define OSPFV2_TE_SUB_ADMIN_GRP   0x0009  ///< Administrative group (color)
#define OSPFV2_TE_SUB_LR_ID       0x000A  ///< Local/Remote identifiers
#define OSPFV2_TE_SUB_PROTECT     0x000B  ///< Link protection type
#define OSPFV2_TE_SUB_IF_SWITCH   0x000C  ///< Interface switching capability

#define OSPFV2_TE_LINK_P2P       1  ///< Point-to-point
#define OSPFV2_TE_LINK_MULTI     2  ///< Multi-access

#define OSPFV2_GR_TLV_GRACE      0x0001  ///< Grace period
#define OSPFV2_GR_TLV_REASON     0x0002  ///< Restart reason
#define OSPFV2_GR_TLV_IF_ADDR    0x0003  ///< Restart interface IP

#define OSPFV2_GR_REASON_UNKNOWN     0  ///< Unknown
#define OSPFV2_GR_REASON_SW_RESTART  1  ///< Software restart
#define OSPFV2_GR_REASON_SW_UPGRADE  2  ///< Software upgrade
#define OSPFV2_GR_REASON_CP_RESTART  3  ///< Control-plane restart

#define OSPFV2_EP_TLV_PREFIX     0x0001  ///< Extended prefix

#define OSPFV2_EP_SUB_SID        0x0001  ///< Prefix SID
#define OSPFV2_EP_SUB_ATTR       0x0002  ///< Prefix attributes

#define OSPFV2_EL_TLV_LINK       0x0001  ///< Extended link

#define OSPFV2_EL_SUB_ADJ_SID    0x0001  ///< Adjacency SID
#define OSPFV2_EL_SUB_LAN_SID    0x0002  ///< LAN adjacency SID
#define OSPFV2_EL_SUB_LINK_MSD   0x0003  ///< Link MSD

#define OSPFV2_EXT_METRIC_E1     0  ///< Type-1 external (internal cost added)
#define OSPFV2_EXT_METRIC_E2     1  ///< Type-2 external (external dominates)

/*
 * @struct Ospfv2LSAHeaderRaw
 */
#pragma pack(push, 1)
struct Ospfv2LSAHeaderRaw
{
    uint8_t age[2];
    uint8_t options;
    uint8_t type;
    uint8_t lsID[4];
    uint8_t advRouter[4];
    uint8_t seqNum[4];
    uint8_t checksum[2];
    uint8_t length[2];
};
#pragma pack(pop)

/*
 * @struct Ospfv2LSAHeader
 * @brief Represents an OSPF (Open Shortest Path First) link state header.
 */
struct Ospfv2LSAHeader
{
    DEFINE_PACKET_HEADER(Ospfv2LSAHeaderRaw);

    uint16_t getAge() const                 { return readU16(raw->age); }
    uint8_t getOptions() const              { return raw->options; }
    uint8_t getType() const                 { return raw->type; }
    uint32_t getLsID() const                { return readU32(raw->lsID); }
    uint32_t getAdvRouter() const           { return readU32(raw->advRouter); }
    uint32_t getSeqNumber() const           { return readU32(raw->seqNum); }
    uint16_t getChecksum() const            { return readU16(raw->checksum); }
    uint16_t getLen() const                 { return readU16(raw->length); }
    
    bool getOptMT() const                   { return raw->options & 0x01; }
    bool getOptE() const                    { return raw->options & 0x02; }
    bool getOptMC() const                   { return raw->options & 0x04; }
    bool getOptNP() const                   { return raw->options & 0x08; }
    bool getOptEA() const                   { return raw->options & 0x10; }
    bool getOptDC() const                   { return raw->options & 0x20; }
    bool getOptO() const                    { return raw->options & 0x40; }

    bool getFlagB(uint8_t flags) const      { return flags & 0x01; }
    bool getFlagE(uint8_t flags) const      { return flags & 0x02; }
    bool getFlagV(uint8_t flags) const      { return flags & 0x04; }
    bool getFlagW(uint8_t flags) const      { return flags & 0x08; }
    bool getFlagN(uint8_t flags) const      { return flags & 0x10; }
    bool getFlagS(uint8_t flags) const      { return flags & 0x20; }
    bool getFlagH(uint8_t flags) const      { return flags & 0x80; }

    void setAge(uint16_t val)
        { writeU16(raw->age, val); }
    void setOptions(uint8_t val)
        { raw->options = val; }
    void setType(uint8_t val)
        { raw->type = val; }
    void setLsID(uint32_t val)
        { writeU32(raw->lsID, val); }
    void setAdvRouter(uint32_t val)
        { writeU32(raw->advRouter, val); }
    void setSeqNum(uint32_t val)
        { writeU32(raw->seqNum, val); }
    void setLen(uint16_t val)
        { writeU16(raw->length, val); }

    void setOptMT(bool val)
        { setBit(&raw->options, 7, val); }
    void setOptE(bool val)
        { setBit(&raw->options, 6, val); }
    void setOptMC(bool val)
        { setBit(&raw->options, 5, val); }
    void setOptNP(bool val)
        { setBit(&raw->options, 4, val); }
    void setOptEA(bool val)
        { setBit(&raw->options, 3, val); }
    void setOptDC(bool val)
        { setBit(&raw->options, 2, val); }
    void setOptO(bool val)
        { setBit(&raw->options, 1, val); }

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


#endif // OSPFV2_LSA_HEADER_HPP
