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
    ManualStart                                               = 1,
    ManualStop                                                = 2,
    AutomaticStart                                            = 3,
    ManualStart_PassiveTcp                                    = 4,
    AutomaticStart_PassiveTcp                                 = 5,
    AutomaticStart_Damp                                       = 6,
    AutomaticStart_DampPassiveTcp                             = 7,
    AutomaticStop                                             = 8,

    // Timer events (9–13)
    ConnectRetryTimer_Expires                                 = 9,
    HoldTimer_Expires                                         = 10,
    KeepaliveTimer_Expires                                    = 11,
    DelayOpenTimer_Expires                                    = 12,
    IdleHoldTimer_Expires                                     = 13,

    // TCP connection events (14–18)
    TcpConnection_Valid                                       = 14,
    Tcp_CR_Invalid                                            = 15,
    Tcp_CR_Acked                                              = 16,
    TcpConnectionConfirmed                                    = 17,
    TcpConnectionFails                                        = 18,

    // BGP message events (19–28)
    BGPOpen                                                   = 19,
    BGPOpen_DelayOpenTimer                                    = 20,
    BGPHeaderErr                                              = 21,
    BGPOpenMsgErr                                             = 22,
    OpenCollisionDump                                         = 23,
    NotifMsgVerErr                                            = 24,
    NotifMsg                                                  = 25,
    KeepAliveMsg                                              = 26,
    UpdateMsg                                                 = 27,
    UpdateMsgErr                                              = 28,

    // Extension events (not in RFC 4271 but used here)
    RouteRefresh                                              = 29,
    BfdDown                                                   = 30,
    BfdUp                                                     = 31,
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
        case FsmEvent::ManualStart:                  return "ManualStart";
        case FsmEvent::ManualStop:                   return "ManualStop";
        case FsmEvent::AutomaticStart:               return "AutomaticStart";
        case FsmEvent::ManualStart_PassiveTcp:       return "ManualStart_PassiveTcp";
        case FsmEvent::AutomaticStart_PassiveTcp:    return "AutomaticStart_PassiveTcp";
        case FsmEvent::AutomaticStart_Damp:          return "AutomaticStart_Damp";
        case FsmEvent::AutomaticStart_DampPassiveTcp: return "AutomaticStart_DampPassiveTcp";
        case FsmEvent::AutomaticStop:                return "AutomaticStop";
        case FsmEvent::ConnectRetryTimer_Expires:    return "ConnectRetryTimer_Expires";
        case FsmEvent::HoldTimer_Expires:            return "HoldTimer_Expires";
        case FsmEvent::KeepaliveTimer_Expires:       return "KeepaliveTimer_Expires";
        case FsmEvent::DelayOpenTimer_Expires:       return "DelayOpenTimer_Expires";
        case FsmEvent::IdleHoldTimer_Expires:        return "IdleHoldTimer_Expires";
        case FsmEvent::TcpConnection_Valid:          return "TcpConnection_Valid";
        case FsmEvent::Tcp_CR_Invalid:               return "Tcp_CR_Invalid";
        case FsmEvent::Tcp_CR_Acked:                 return "Tcp_CR_Acked";
        case FsmEvent::TcpConnectionConfirmed:       return "TcpConnectionConfirmed";
        case FsmEvent::TcpConnectionFails:           return "TcpConnectionFails";
        case FsmEvent::BGPOpen:                      return "BGPOpen";
        case FsmEvent::BGPOpen_DelayOpenTimer:       return "BGPOpen_DelayOpenTimer";
        case FsmEvent::BGPHeaderErr:                 return "BGPHeaderErr";
        case FsmEvent::BGPOpenMsgErr:                return "BGPOpenMsgErr";
        case FsmEvent::OpenCollisionDump:            return "OpenCollisionDump";
        case FsmEvent::NotifMsgVerErr:               return "NotifMsgVerErr";
        case FsmEvent::NotifMsg:                     return "NotifMsg";
        case FsmEvent::KeepAliveMsg:                 return "KeepAliveMsg";
        case FsmEvent::UpdateMsg:                    return "UpdateMsg";
        case FsmEvent::UpdateMsgErr:                 return "UpdateMsgErr";
        case FsmEvent::RouteRefresh:                 return "RouteRefresh";
        case FsmEvent::BfdDown:                      return "BfdDown";
        case FsmEvent::BfdUp:                        return "BfdUp";
        default:                                     return "Unknown";
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
