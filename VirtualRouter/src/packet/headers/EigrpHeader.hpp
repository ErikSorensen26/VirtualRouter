// EigrpHeader.hpp

#ifndef EIGRP_HEADER_HPP
#define EIGRP_HEADER_HPP

#include <vector>

#include "packet/HeaderHelpers.hpp"
#include "packet/TlvOptions.hpp"

#define EIGRP_TYPE_UPDATE               0x01U ///< EIGRP Update message type
#define EIGRP_TYPE_REQUEST              0x02U ///< EIGRP Request message type
#define EIGRP_TYPE_QUERY                0x03U ///< EIGRP Query message type
#define EIGRP_TYPE_REPLY                0x04U ///< EIGRP Reply message type
#define EIGRP_TYPE_HELLO                0x05U ///< EIGRP Hello message type
#define EIGRP_TYPE_SIA_QUERY            0xA0U ///< EIGRP SIA Query message type
#define EIGRP_TYPE_SIA_REPLY            0xA1U ///< EIGRP SIA Reply message type

#define EIGRP_OPTION_PARAMETER                  0x0001U ///< EIGRP Option for Parameters
#define EIGRP_OPTION_AUTHENTICATION             0x0002U ///< EIGRP Option for Authentication
#define EIGRP_OPTION_SEQUENCE                   0x0003U ///< EIGRP Option for Excluded Conditional Neighbors
#define EIGRP_OPTION_VERSION                    0x0004U ///< EIGRP Option for Version
#define EIGRP_OPTION_MULTICAST_SEQUENCE         0x0005U ///< EIGRP Option for Conditional Sequence
#define EIGRP_OPTION_STUB                       0x0006U ///< EIGRP Option for Stub
#define EIGRP_OPTION_LEGACY_INTERNAL_ROUTE      0x0102U ///< EIGRP Option for Legacy Internal Route
#define EIGRP_OPTION_LEGACY_EXTERNAL_ROUTE      0x0103U ///< EIGRP Option for Legacy External Route
#define EIGRP_OPTION_LEGACY_INTERNAL_ROUTE_V6   0x0402U ///< EIGRP Option for Legacy Internal Route v6
#define EIGRP_OPTION_LEGACY_EXTERNAL_ROUTE_V6   0x0403U ///< EIGRP Option for Legacy External Route v6
#define EIGRP_OPTION_INTERNAL_ROUTE             0x0602U ///< EIGRP Option for Internal Route
#define EIGRP_OPTION_EXTERNAL_ROUTE             0x0603U ///< EIGRP Option for External Route

#define EIGRP_VERSION_RELEASE           0x0C04U ///< EIGRP Release Version
#define EIGRP_VERSION_TLS               0x0200U ///< EIGRP TLS Version

#define EIGRP_EXTERNAL_IGRP             0x01U ///< EIGRP External Protocol IGRP
#define EIGRP_EXTERNAL_EIGRP            0x02U ///< EIGRP External Protocol EIGRP
#define EIGRP_EXTERNAL_STATIC           0x03U ///< EIGRP External Protocol Static
#define EIGRP_EXTERNAL_RIP              0x04U ///< EIGRP External Protocol RIP
#define EIGRP_EXTERNAL_OSPF             0x06U ///< EIGRP External Protocol OSPF
#define EIGRP_EXTERNAL_ISIS             0x07U ///< EIGRP External Protocol ISIS
#define EIGRP_EXTERNAL_BGP              0x09U ///< EIGRP External Protocol BGP
#define EIGRP_EXTERNAL_CONNECTED        0x0BU ///< EIGRP External Protocol Connected

#define EIGRP_ENCODING_IPV4             0x01U ///< EIGRP Destination Assignment Encoding IPv4
#define EIGRP_ENCODING_IPV6             0x02U ///< EIGRP Destination Assignment Encoding IPv6
#define EIGRP_ENCODING_COMMON_SERVICE   0x4000U ///< EIGRP Destination Assignment Encoding Common Service
#define EIGRP_ENCODING_IPV4_FAMILY      0x4001U ///< EIGRP Destination Assignment Encoding IPv4 Family
#define EIGRP_ENCODING_IPV6_FAMILY      0x4002U ///< EIGRP Destination Assignemnt Encoding IPv6 Family

#define EIGRP_EXCOMM_EIGRP              0x00U ///< EIGRP Community Attribute for EIGRP
#define EIGRP_EXCOMM_DAD                0x01U ///< EIGRP Community Attribute for DAD
#define EIGRP_EXCOMM_VRHB               0x02U ///< EIGRP Community Attribute for VRHB
#define EIGRP_EXCOMM_SRLM               0x03U ///< EIGRP Community Attribute for SRLM
#define EIGRP_EXCOMM_SAR                0x04U ///< EIGRP Community Attribute for SAR
#define EIGRP_EXCOMM_RPM                0x05U ///< EIGRP Community Attribute for RPM
#define EIGRP_EXCOMM_VRR                0x06U ///< EIGRP Community Attribute for VRR

#define EIGRP_DAMPENING_FLAP_PENALTY        1000 ///< EIGRP Dampening flap penalty
#define EIGRP_DAMPENING_SUPPRESS_THRESHOLD  2000 ///< Suppression Threshold from penalty
#define EIGRP_DAMPENING_REUSE_THRESHOLD      750 ///< Penalty needed to be unsuppressed
#define EIGRP_DAMPENING_DECAY_THRESHOLD        5 ///< Penalty decays every 5 seconds
#define EIGRP_DAMPENING_HALFLIFE_TIME         15 ///< Penalty is halfed every 15 seconds
#define EIGRP_DAMPENING_MAX_SUPPRESSION_TIME  10 ///< Maximum time a route can be suppressed

inline constexpr uint8_t EIGRP_MULTICAST_ADDRESS[4] = { 0xE0, 0x00, 0x00, 0x0A }; ///< EIGRP Multicast IPv4 Address.
inline constexpr uint8_t EIGRP_MULTICAST_ADDRESS_V6[16] = { 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A }; ///< EIGRP Multicast IPv6 Address.

/**
 * @struct EigrpHeaderRaw
 */
#pragma pack(push, 1) 
struct EigrpHeaderRaw
{
    uint8_t version;
    uint8_t opcode;
    uint8_t checksum[2];
    uint8_t flags[4];
    uint8_t sequence[4];
    uint8_t ack[4];
    uint8_t virtualRouterId[2];
    uint8_t autonomousSystem[2];
};
#pragma pack(pop)

/**
 * @struct EigrpHeader
 * @brief Represents an EIGRP (Enhanced Interior Gateway Routing Protocol) header.
 */
struct EigrpHeader
{
    DEFINE_PACKET_HEADER(EigrpHeaderRaw);

    uint8_t getVersion() const             { return raw->version; }
    uint8_t getOpcode() const              { return raw->opcode; }
    uint32_t getSequence() const           { return readU32(raw->sequence); }
    uint32_t getAck() const                { return readU32(raw->ack); }
    uint16_t getVirtualRouterID() const    { return readU16(raw->virtualRouterId); }
    uint16_t getAutonomousSystem() const   { return readU16(raw->autonomousSystem); }

    bool getFlagInit() const               { return raw->flags[3] & 0x01; }
    bool getFlagCondRecv() const           { return raw->flags[3] & 0x02; }
    bool getFlagRestart() const            { return raw->flags[3] & 0x04; }
    bool getFlagEndOfTable() const         { return raw->flags[3] & 0x08; }

    void setVersion(uint8_t val)
        { raw->version = val; }
    void setOpcode(uint8_t val) 
        { raw->opcode = val; }
    void setSequence(uint32_t val) 
        { writeU32(raw->sequence, val); }
    void setAck(uint32_t val)
        { writeU32(raw->ack, val); }
    void setVirtualRouterId(uint16_t val)
        { writeU16(raw->virtualRouterId, val); }
    void setAutonomousSystem(uint16_t val)
        { writeU16(raw->autonomousSystem, val); }

    void setFlagInit(bool val)
        { setBit(raw->flags, 31, val); }
    void setFlagCondRecv(bool val)
        { setBit(raw->flags, 30, val); }
    void setFlagRestart(bool val)
        { setBit(raw->flags, 29, val); }
    void setFlagEndOfTable(bool val)
        { setBit(raw->flags, 28, val); }
};

inline bool parseEigrpOptions(const uint8_t* data, size_t size, std::vector<TLV16Option>& outOptions)
{
    size_t offset = 0;
    while (offset + 4 <= size)
    {
        const uint16_t type = readU16(data + offset);
        const uint16_t length = readU16(data + offset + 2);
        if (length < 4 || offset + length > size) return false;

        uint8_t* value = const_cast<uint8_t*>(data) + offset + 4;
        size_t valueSize = length - 4;

        outOptions.emplace_back(type, length, value, valueSize);
        
        offset += length;
    }
    return offset == size;
}

#endif // EIGRP_HEADER_HPP
