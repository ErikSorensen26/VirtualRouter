/**
 * @file BgpHeader.hpp
 */

// BgpHeader.hpp

#ifndef BGP_HEADER_HPP
#define BGP_HEADER_HPP

#include <span>
#include <ByteUtils.hpp>
#include "packet/HeaderHelpers.hpp"

// BGP message types
#define BGP_TYPE_OPEN         0x01
#define BGP_TYPE_UPDATE       0x02
#define BGP_TYPE_NOTIFICATION 0x03
#define BGP_TYPE_KEEPALIVE    0x04
#define BGP_TYPE_ROUTE_REFRESH 0x05

// BGP open optional parameter types
#define BGP_PARAMETER_CAPABILITY 0x02

// BGP capability codes
#define BGP_CAPABILITY_MULTIPROTOCOL         0x01
#define BGP_CAPABILITY_ROUTE_REFRESH         0x02
#define BGP_CAPABILITY_OUTBOUND_FILTER       0x03
#define BGP_CAPABILITY_EXTENDED_NEXT_HOP     0x05
#define BGP_CAPABILITY_EXTENDED_MESSAGE      0x06
#define BGP_CAPABILITY_BGP_SEC               0x07
#define BGP_CAPABILITY_MULTIPLE_LABELS       0x08
#define BGP_CAPABILITY_GRACEFUL_RESTART      64
#define BGP_CAPABILITY_32_BIT_AS             65
#define BGP_CAPABILITY_MULTI_SESSION         68
#define BGP_CAPABILITY_ADD_PATH              69
#define BGP_CAPABILITY_ENHANCED_ROUTE_REFRESH 70
#define BGP_CAPABILITY_LLGR                  71
#define BGP_CAPABILITY_FQDN                  73
#define BGP_CAPABILITY_LINK_LOCAL_NEXT_HOP   77

// AFI values
#define BGP_AFI_IPV4   0x0001
#define BGP_AFI_IPV6   0x0002
#define BGP_AFI_L2VPN  0x0019

// SAFI values
#define BGP_SAFI_UNICAST              0x01
#define BGP_SAFI_MULTICAST            0x02
#define BGP_SAFI_MPLS_LABELED_UNICAST 0x04
#define BGP_SAFI_MCAST_VPM            0x05
#define BGP_SAFI_PSEUDOWIRE           0x06
#define BGP_SAFI_ENCAPSULATION        0x07
#define BGP_SAFI_MPLS_VPN             128
#define BGP_SAFI_EVPN                 0x46
#define BGP_SAFI_BGP_LS               71
#define BGP_SAFI_BGP_LS_VPN           72
#define BGP_SAFI_SR_POLICY            0x49
#define BGP_SAFI_FLOW_SPEC            133
#define BGP_SAFI_FLOW_SPEC_VPN        134

// ORIGIN attribute values
#define BGP_ORIGIN_IGP        0x00
#define BGP_ORIGIN_EGP        0x01
#define BGP_ORIGIN_INCOMPLETE 0x02

// Path attribute type codes
#define BGP_ATTR_ORIGIN                    1
#define BGP_ATTR_AS_PATH                   2
#define BGP_ATTR_NEXT_HOP                  3
#define BGP_ATTR_MULTI_EXIT_DISC           4
#define BGP_ATTR_LOCAL_PREF                5
#define BGP_ATTR_ATOMIC_AGGREGATE          6
#define BGP_ATTR_AGGREGATOR                7
#define BGP_ATTR_COMMUNITIES               8
#define BGP_ATTR_ORIGINATOR_ID             9
#define BGP_ATTR_CLUSTER_LIST              10
#define BGP_ATTR_MP_REACH_NLRI             14
#define BGP_ATTR_MP_UNREACH_NLRI           15
#define BGP_ATTR_EXTENDED_COMMUNITIES      16
#define BGP_ATTR_AS4_PATH                  17
#define BGP_ATTR_AS4_AGGREGATOR            18
#define BGP_ATTR_PMSI_TUNNEL               22
#define BGP_ATTR_TUNNEL_ENCAP              23
#define BGP_ATTR_IPV6_EXTENDED_COMMUNITIES 25
#define BGP_ATTR_AIGP                      26
#define BGP_ATTR_LARGE_COMMUNITIES         32
#define BGP_ATTR_BGPSEC_PATH               33
#define BGP_ATTR_OTC                       35
#define BGP_ATTR_PREFIX_SID                40
#define BGP_ATTR_ATTR_SET                  128

// Path attribute flags
#define BGP_ATTR_FLAG_OPTIONAL        0x80
#define BGP_ATTR_FLAG_TRANSITIVE      0x40
#define BGP_ATTR_FLAG_PARTIAL         0x20
#define BGP_ATTR_FLAG_EXTENDED_LENGTH 0x10

// AS_PATH segment types
#define BGP_AS_SET             1
#define BGP_AS_SEQUENCE        2
#define BGP_AS_CONFED_SEQUENCE 3
#define BGP_AS_CONFED_SET      4

// Well-known community values (RFC 1997)
#define BGP_COMMUNITY_NO_EXPORT          0xFFFFFF01u
#define BGP_COMMUNITY_NO_ADVERTISE       0xFFFFFF02u
#define BGP_COMMUNITY_NO_EXPORT_SUBCONFED 0xFFFFFF03u
#define BGP_COMMUNITY_BLACKHOLE          0xFFFF029Au

// Route-refresh subtypes (Enhanced Route Refresh, RFC 7313)
#define BGP_ROUTE_REFRESH_NORMAL 0
#define BGP_ROUTE_REFRESH_BORR   1
#define BGP_ROUTE_REFRESH_EORR   2

// ADD-PATH send/receive flags
#define BGP_ADD_PATH_RECEIVE 0x01
#define BGP_ADD_PATH_SEND    0x02
#define BGP_ADD_PATH_BOTH    0x03

// ORF (Outbound Route Filtering) flags and types (RFC 5291/5292)
#define BGP_ORF_RECEIVE          0x01
#define BGP_ORF_SEND             0x02
#define BGP_ORF_BOTH             0x03
#define BGP_ORF_TYPE_PREFIX_LIST 64

// ORF action values (top 2 bits of action/match byte)
#define BGP_ORF_ACTION_ADD        0x00
#define BGP_ORF_ACTION_REMOVE     0x01
#define BGP_ORF_ACTION_REMOVE_ALL 0x02

// ORF match values (bit 0 of action/match byte)
#define BGP_ORF_MATCH_PERMIT 0x00
#define BGP_ORF_MATCH_DENY   0x01

// ORF when-to-refresh (byte 2 of ROUTE_REFRESH carrying ORF)
#define BGP_ORF_WHEN_IMMEDIATE 0x00
#define BGP_ORF_WHEN_DEFERRED  0x01

// Notification error codes (high byte) and subcodes (low byte)
#define BGP_NOTIFICATION_HEADER 0x01
#define BGP_NOTIFICATION_OPEN   0x02
#define BGP_NOTIFICATION_UPDATE 0x03
#define BGP_NOTIFICATION_HOLD   0x04
#define BGP_NOTIFICATION_FSM    0x05
#define BGP_NOTIFICATION_CEASE  0x06
#define BGP_NOTIFICATION_REFRESH 0x07

// Header error subcodes
#define BGP_NOTIFICATION_HEADER_CONNECTION_NOT_SYNCED 0x0101
#define BGP_NOTIFICATION_HEADER_BAD_MESSAGE_LENGTH    0x0102
#define BGP_NOTIFICATION_HEADER_BAD_MESSAGE_TYPE      0x0103

// Open error subcodes
#define BGP_NOTIFICATION_OPEN_UNSPECIFIED            0x0200
#define BGP_NOTIFICATION_OPEN_UNSUPPORTED_VERSION    0x0201
#define BGP_NOTIFICATION_OPEN_BAD_PEER_AS            0x0202
#define BGP_NOTIFICATION_OPEN_BAD_IDENTIFIER         0x0203
#define BGP_NOTIFICATION_OPEN_UNSUPPORTED_PARAMETER  0x0204
#define BGP_NOTIFICATION_OPEN_AUTHENTICATION_FAILURE 0x0205
#define BGP_NOTIFICATION_OPEN_UNACCEPTABLE_HOLD      0x0206
#define BGP_NOTIFICATION_OPEN_UNSUPPORTED_CAPABILITY 0x0207
#define BGP_NOTIFICATION_OPEN_ROLE_MISMATCH          0x0208

// Update error subcodes
#define BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST      0x0301
#define BGP_NOTIFICATION_UPDATE_UNRECOGNIZED_ATTR        0x0302
#define BGP_NOTIFICATION_UPDATE_MISSING_ATTR             0x0303
#define BGP_NOTIFICATION_UPDATE_ATTR_FLAG                0x0304
#define BGP_NOTIFICATION_UPDATE_ATTR_LENGTH              0x0305
#define BGP_NOTIFICATION_UPDATE_INVALID_ORIGIN           0x0306
#define BGP_NOTIFICATION_UPDATE_AS_ROUTING_LOOP          0x0307
#define BGP_NOTIFICATION_UPDATE_INVALID_NEXT_HOP_ATTR    0x0308
#define BGP_NOTIFICATION_UPDATE_OPT_ATTR                 0x0309
#define BGP_NOTIFICATION_UPDATE_INVALID_NETWORK          0x030A
#define BGP_NOTIFICATION_UPDATE_MALFORMED_AS_PATH        0x030B

// Hold timer error
#define BGP_NOTIFICATION_HOLD_TIMER_EXPIRED 0x0400

// FSM error subcodes
#define BGP_NOTIFICATION_FSM_UNSPECIFIED   0x0500
#define BGP_NOTIFICATION_FSM_OPEN_SENT     0x0501
#define BGP_NOTIFICATION_FSM_OPEN_CONFIRM  0x0502
#define BGP_NOTIFICATION_FSM_ESTABLISH     0x0503

// Cease subcodes (RFC 4486)
#define BGP_NOTIFICATION_CEASE_UNSPECIFIC            0x0600
#define BGP_NOTIFICATION_CEASE_MAX_PREFIXES          0x0601
#define BGP_NOTIFICATION_CEASE_ADMIN_SHUT            0x0602
#define BGP_NOTIFICATION_CEASE_PEER_DE_CONFIGURED    0x0603
#define BGP_NOTIFICATION_CEASE_ADMIN_RESET           0x0604
#define BGP_NOTIFICATION_CEASE_CONNECTION_REJECT     0x0605
#define BGP_NOTIFICATION_CEASE_OTHER_CONFIG          0x0606
#define BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION  0x0607
#define BGP_NOTIFICATION_CEASE_OUT_OF_RESOURCES      0x0608
#define BGP_NOTIFICATION_CEASE_HARD_RESET            0x0609
#define BGP_NOTIFICATION_CEASE_BFD_DOWN              0x060A
#define BGP_NOTIFICATION_CEASE_GRACEFUL_ADMIN_SHUT   0x060B
#define BGP_NOTIFICATION_CEASE_PEER_RESTARTING       0x060C
#define BGP_NOTIFICATION_CEASE_GRACEFUL_RESTART_TIMEOUT 0x060D

// Route refresh error subcodes
#define BGP_NOTIFICATION_REFRESH_INVALID_LENGTH 0x0701
#define BGP_NOTIFICATION_REFRESH_INVALID_AFI    0x0702
#define BGP_NOTIFICATION_REFRESH_INVALID_SAFI   0x0703

// Helper macros to split combined code/subcode
#define BGP_GET_TYPE(val)     (uint8_t)((val >> 8) & 0xFF)
#define BGP_GET_SUB_TYPE(val) (uint8_t)(val & 0xFF)

#define BGP_VERSION 0x04

constexpr uint8_t BGP_MARKER[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

namespace packet
{

/**
 * @struct BgpHeaderRaw
 * @brief Represents a raw BGP message header.
 * @ingroup PACKET_HEADERS
 */
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
 * @brief Represents a BGP message header (marker + length + type).
 * @ingroup PACKET_HEADERS
 */
struct BgpHeader
{
    DEFINE_PACKET_HEADER(BgpHeaderRaw);

    uint8_t* getMarker() const { return raw->marker; }
    uint16_t getLength() const { return utils::readU16(raw->length); }
    uint8_t  getType()   const { return raw->type; }

    void setMarker()             { std::memset(raw->marker, 0xff, sizeof(raw->marker)); }
    void setLength(uint16_t val) { utils::writeU16(raw->length, val); }
    void setType(uint8_t val)    { raw->type = val; }
};

} // namespace packet

#endif // BGP_HEADER_HPP
