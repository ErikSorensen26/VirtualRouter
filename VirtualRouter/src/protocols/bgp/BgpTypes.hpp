// BgpTypes.hpp

#ifndef BGP_TYPES_HPP
#define BGP_TYPES_HPP

#include <cstdint>
#include <vector>
#include <IPAddress.hpp>

#include "packet/headers/BgpHeader.hpp"

namespace BGP
{
constexpr uint16_t kMinMessageLen = BgpHeader::fixedSize;
constexpr uint16_t kMaxMessageLen = 4096;
constexpr uint16_t kExtendedMessageLen = 65535;
constexpr uint32_t kAsTrans = 23456;

enum class FsmState : uint8_t
{
    IDLE,
    CONNECT,
    ACTIVE,
    OPEN_SENT,
    OPEN_CONFIRMED,
    ESTABLISHED,
};

enum class FsmEvent : uint8_t
{
    // Administrative events (1–8)
    MANUAL_START                                              = 1,
    MANUAL_STOP                                               = 2,
    AUTOMATIC_START                                           = 3,
    MANUAL_START_PASSIVE_TCP                                  = 4,
    AUTOMATIC_START_PASSIVE_TCP                               = 5,
    AUTOMATIC_START_DAMP                                      = 6,
    AUTOMATIC_START_DAMP_PASSIVE_TCP                          = 7,
    AUTOMATIC_STOP                                            = 8,

    // Timer events (9–13)
    CONNECTION_RETRY_TIMER_EXPIRES                            = 9,
    HOLD_TIMER_EXPIRES                                        = 10,
    KEEPALIVE_TIMER_EXPIRES                                   = 11,
    DELAY_OPEN_TIMER_EXPIRES                                  = 12,
    IDLE_HOLD_TIMER_EXPIRES                                   = 13,

    // TCP connection events (14–18)
    TCP_CONNECTION_VALID                                      = 14,
    TCP_CR_INVALID                                            = 15,
    TCP_CR_ACKED                                              = 16,
    TCP_CONNECTION_CONFIRMED                                  = 17,
    TCP_CONNECTION_FAILS                                      = 18,

    // BGP message events (19–28)
    BGP_OPEN                                                  = 19,
    BGP_OPEN_DELAY_OPEN_TIMER                                 = 20,
    BGP_HEADER_ERR                                            = 21,
    BGP_OPEN_MSG_ERR                                          = 22,
    OPEN_COLLISION_DUMP                                       = 23,
    NOTIF_MSG_VER_ERR                                         = 24,
    NOTIF_MSG                                                 = 25,
    KEEPALIVE_MSG                                             = 26,
    UPDATE_MSG                                                = 27,
    UPDATE_MSG_ERR                                            = 28,

    // Extension events (not in RFC 4271 but used here)
    ROUTE_REFRESH                                             = 29,
    BFD_DOWN                                                  = 30,
    BFD_UP                                                    = 31,
    MAX_PREFIX_REACHED                                        = 32,
};

struct Notification
{
    uint16_t code = 0;
    std::vector<uint8_t> data = {};
};

struct AfiSafi
{
    uint16_t afi = 0;
    uint8_t safi = 0;

    uint32_t flatten() const
    {
        uint32_t flat = afi;
        flat |= uint32_t(safi) << 16;
        return flat;
    }

    bool operator==(const AfiSafi& other) const noexcept
    {
        return afi == other.afi && safi == other.safi;
    }

    bool operator!=(const AfiSafi& other) const noexcept
    {
        return !(*this == other);
    }
};

struct NeighborKey
{
    IPAddress ipAddr;
    uint16_t afi;
    uint8_t safi;

    bool operator==(const NeighborKey& other) const noexcept
    {
        return ipAddr == other.ipAddr &&
               afi == other.afi &&
               safi == other.safi;
    }
};

struct PeerKey
{
    PeerKey() = default;
    PeerKey(const NeighborKey& key, uint32_t rid)
        : rid(rid), afi(key.afi), safi(key.safi) {}

    uint32_t rid;
    uint16_t afi;
    uint8_t safi;

    bool operator==(const PeerKey& other) const noexcept
    {
        return rid == other.rid &&
               afi == other.afi &&
               safi == other.safi;
    }
};

// Subtype for a ROUTE_REFRESH message (RFC 7313).
enum class RouteRefreshReason : uint8_t
{
    Normal = BGP_ROUTE_REFRESH_NORMAL,  // RFC 2918 plain route refresh
    Borr   = BGP_ROUTE_REFRESH_BORR,    // begin-of-route-refresh
    Eorr   = BGP_ROUTE_REFRESH_EORR,    // end-of-route-refresh
};

inline AddressFamily toAddressFamily(const AfiSafi& family) noexcept {
    if (family.afi == BGP_AFI_IPV4)
        return AddressFamily::IPv4;
    if (family.afi == BGP_AFI_IPV6)
        return AddressFamily::IPv6;
    return AddressFamily::NONE;
}

inline const char* fsmStateName(FsmState s) noexcept
{
    switch (s) {
        case FsmState::IDLE:          return "Idle";
        case FsmState::CONNECT:       return "Connect";
        case FsmState::ACTIVE:        return "Active";
        case FsmState::OPEN_SENT:     return "OpenSent";
        case FsmState::OPEN_CONFIRMED: return "OpenConfirm";
        case FsmState::ESTABLISHED:   return "Established";
        default:                      return "Unknown";
    }
}

inline const char* fsmEventName(FsmEvent e) noexcept
{
    switch (e) {
        case FsmEvent::MANUAL_START:                      return "ManualStart";
        case FsmEvent::MANUAL_STOP:                       return "ManualStop";
        case FsmEvent::AUTOMATIC_START:                   return "AutomaticStart";
        case FsmEvent::MANUAL_START_PASSIVE_TCP:          return "ManualStart_PassiveTcp";
        case FsmEvent::AUTOMATIC_START_PASSIVE_TCP:       return "AutomaticStart_PassiveTcp";
        case FsmEvent::AUTOMATIC_START_DAMP:              return "AutomaticStart_Damp";
        case FsmEvent::AUTOMATIC_START_DAMP_PASSIVE_TCP:  return "AutomaticStart_DampPassiveTcp";
        case FsmEvent::AUTOMATIC_STOP:                    return "AutomaticStop";
        case FsmEvent::CONNECTION_RETRY_TIMER_EXPIRES:    return "ConnectRetryTimer_Expires";
        case FsmEvent::HOLD_TIMER_EXPIRES:                return "HoldTimer_Expires";
        case FsmEvent::KEEPALIVE_TIMER_EXPIRES:           return "KeepaliveTimer_Expires";
        case FsmEvent::DELAY_OPEN_TIMER_EXPIRES:          return "DelayOpenTimer_Expires";
        case FsmEvent::IDLE_HOLD_TIMER_EXPIRES:           return "IdleHoldTimer_Expires";
        case FsmEvent::TCP_CONNECTION_VALID:              return "TcpConnection_Valid";
        case FsmEvent::TCP_CR_INVALID:                    return "Tcp_CR_Invalid";
        case FsmEvent::TCP_CR_ACKED:                      return "Tcp_CR_Acked";
        case FsmEvent::TCP_CONNECTION_CONFIRMED:          return "TcpConnectionConfirmed";
        case FsmEvent::TCP_CONNECTION_FAILS:              return "TcpConnectionFails";
        case FsmEvent::BGP_OPEN:                          return "BGPOpen";
        case FsmEvent::BGP_OPEN_DELAY_OPEN_TIMER:         return "BGPOpen_DelayOpenTimer";
        case FsmEvent::BGP_HEADER_ERR:                    return "BGPHeaderErr";
        case FsmEvent::BGP_OPEN_MSG_ERR:                  return "BGPOpenMsgErr";
        case FsmEvent::OPEN_COLLISION_DUMP:               return "OpenCollisionDump";
        case FsmEvent::NOTIF_MSG_VER_ERR:                 return "NotifMsgVerErr";
        case FsmEvent::NOTIF_MSG:                         return "NotifMsg";
        case FsmEvent::KEEPALIVE_MSG:                     return "KeepAliveMsg";
        case FsmEvent::UPDATE_MSG:                        return "UpdateMsg";
        case FsmEvent::UPDATE_MSG_ERR:                    return "UpdateMsgErr";
        case FsmEvent::ROUTE_REFRESH:                     return "RouteRefresh";
        case FsmEvent::BFD_DOWN:                          return "BfdDown";
        case FsmEvent::BFD_UP:                            return "BfdUp";
        case FsmEvent::MAX_PREFIX_REACHED:                return "MaxPrefixReached";
        default:                                          return "Unknown";
    }
}
}

namespace std
{
template <>
struct hash<BGP::AfiSafi>
{
    size_t operator()(const BGP::AfiSafi& family) const noexcept
    {
        uint32_t v = (static_cast<uint32_t>(family.afi) << 8) | family.safi;
        return std::hash<uint32_t>{}(v);
    }
};

template <>
struct hash<BGP::NeighborKey>
{
    size_t operator()(const BGP::NeighborKey& k) const noexcept
    {
        size_t h1 = std::hash<IPAddress>{}(k.ipAddr);
        size_t h2 = std::hash<uint32_t>{}((static_cast<uint32_t>(k.afi) << 8) | k.safi);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

template <>
struct hash<BGP::PeerKey>
{
    size_t operator()(const BGP::PeerKey& k) const noexcept
    {
        size_t h1 = std::hash<uint32_t>{}(k.rid);
        size_t h2 = std::hash<uint32_t>{}((static_cast<uint32_t>(k.afi) << 8) | k.safi);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
}

#endif // BGP_TYPES_HPP
