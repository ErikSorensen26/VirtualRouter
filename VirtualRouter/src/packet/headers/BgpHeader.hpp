// BgpHeader.hpp

#ifndef BGP_HEADER_HPP
#define BGP_HEADER_HPP

#include <span>

#include "packet/HeaderHelpers.hpp"

#define BGP_TYPE_OPEN 0x01
#define BGP_TYPE_UPDATE 0x02
#define BGP_TYPE_NOTIFICATION 0x03
#define BGP_TYPE_KEEP_ALIVE 0x04
#define BGP_TYPE_ROUTE_REFRESH 0x05

#define BGP_PARAMETER_CAPABILITY 0x02

#define BGP_CAPABILITY_MULTIPROTOCOL 0x01
#define BGP_CAPABILITY_ROUTE_REFRESH 0x02
#define BGP_CAPABILITY_OUTBOUND_FILTER 0x03
#define BGP_CAPABILITY_EXTENDED_NEXT_HOP 0x05
#define BGP_CAPABILITY_EXTENDED_MESSAGE 0x06
#define BGP_CAPABILITY_BGP_SEC 0x07
#define BGP_CAPABILITY_MULTIPLE_LABELS 0x08
#define BGP_CAPABILITY_GRACEFUL_RESTART 64
#define BGP_CAPABILITY_32_BIT_AS 65
#define BGP_CAPABILITY_ADD_PATH 69
#define BGP_CAPABILITY_ENHANCED_ROUTE_REFRESH 70
#define BGP_CAPABILITY_LLGR 71
#define BGP_CAPABILITY_LINK_LOCAL_NEXT_HOP 77

#define BGP_AFI_IPV4 0x0001
#define BGP_AFI_IPV6 0x0002

#define BGP_SAFI_UNICAST 0x01
#define BGP_SAFI_MULTICAST 0x02
#define BGP_SAFI_MPLS_LABELED_UNICAST 0x04
#define BGP_SAFI_MCAST_VPM 0x05
#define BGP_SAFI_PSEUDOWIRE 0x06
#define BGP_SAFI_ENCAPSULATION 0x07
#define BGP_SAFI_MPLS_VPN 0x08
#define BGP_SAFI_BGP_LS 0x09
#define BGP_SAFI_EVPN 0x46
#define BGP_SAFI_BGP_LS_VPN 0x47
#define BGP_SAFI_SR_POLICY 0x49
#define BGP_SAFI_FLOW_SPEC 0x85
#define BGP_SAFI_FLOW_SPEC_VPN 0x86

#define BGP_VERSION 0x04

constexpr uint8_t BGP_MARKER[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

#pragma pack(push, 1)
struct BgpHeaderRaw
{
    uint8_t marker[16];
    uint8_t length[2];
    uint8_t type;
};
#pragma pack(pop)

/**
 * @struct BgpHeader
 * @brief Represents a BGP (Border Gateway Protocol) header.
 */
struct BgpHeader
{
    DEFINE_PACKET_HEADER(BgpHeaderRaw);

    uint8_t* getMarker() const { return raw->marker; }
    uint16_t getLength() const { return readU16(raw->length); }
    uint8_t getType() const { return raw->type; }

    void setMarker()
        { std::memset(raw->marker, 0xff, sizeof(raw->marker)); }
    void setLength(uint16_t val)
        { writeU16(raw->length, val); }
    void setType(uint8_t val)
        { raw->type = val; }
};

#endif // BGP_HEADER_HPP
