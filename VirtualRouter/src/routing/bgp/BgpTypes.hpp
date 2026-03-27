/**
 * @file BgpTypes.hpp
 * @brief Core BGP types: FSM states/events, AFI/SAFI keys, and protocol constants.
 */

#ifndef BGP_TYPES_HPP
#define BGP_TYPES_HPP

#include <cstdint>
#include <vector>
#include <IPAddress.h>

#include "packet/headers/BgpHeader.hpp"

namespace routing::bgp
{
constexpr uint16_t kMinMessageLen    = packet::BgpHeader::fixedSize; ///< Minimum valid BGP message length (19 bytes: marker + length + type).
constexpr uint16_t kMaxMessageLen    = 4096;                         ///< Default maximum BGP message length (RFC 4271 §4).
constexpr uint16_t kExtendedMessageLen = 65535;                      ///< Extended message length when the Extended Message capability is negotiated (RFC 8654).
constexpr uint32_t kAsTrans          = 23456;                        ///< AS_TRANS placeholder used in 2-byte AS fields when the real AS exceeds 16 bits (RFC 4893).

/**
 * @brief RFC 4271 finite-state machine states.
 * @ingroup BGP
 *
 * The FSM transitions between these states in response to @ref FsmEvent values.
 * Only one state is active at a time per @ref Session.
 */
enum class FsmState : uint8_t
{
    IDLE,           ///< Initial/reset state; no TCP activity.
    CONNECT,        ///< Actively attempting a TCP connection.
    ACTIVE,         ///< TCP connection failed; retrying or waiting passively.
    OPEN_SENT,      ///< TCP connected; OPEN message sent, waiting for peer's OPEN.
    OPEN_CONFIRMED, ///< Both OPENs exchanged; waiting for first KEEPALIVE.
    ESTABLISHED,    ///< Full session up; UPDATEs and KEEPALIVEs are exchanged.
};

/**
 * @brief RFC 4271 FSM event codes and local extensions.
 * @ingroup BGP
 *
 * Event numbers 1–28 match the RFC 4271 §8.1 event numbering exactly so that
 * log messages can be correlated with the specification.  Events 29–32 are
 * local extensions that are not defined in RFC 4271.
 */
enum class FsmEvent : uint8_t
{
    // Administrative events (1–8)
    MANUAL_START                                              = 1,  ///< Operator issues "neighbor activate" or equivalent.
    MANUAL_STOP                                               = 2,  ///< Operator issues "neighbor shutdown".
    AUTOMATIC_START                                           = 3,  ///< System-initiated start (e.g. after idle-hold).
    MANUAL_START_PASSIVE_TCP                                  = 4,  ///< Start in passive mode (wait for inbound TCP only).
    AUTOMATIC_START_PASSIVE_TCP                               = 5,  ///< Automatic passive-mode start.
    AUTOMATIC_START_DAMP                                      = 6,  ///< Start with route dampening enabled.
    AUTOMATIC_START_DAMP_PASSIVE_TCP                          = 7,  ///< Passive start with dampening.
    AUTOMATIC_STOP                                            = 8,  ///< System-triggered stop (e.g. max-prefix exceeded with restart).

    // Timer events (9–13)
    CONNECTION_RETRY_TIMER_EXPIRES                            = 9,  ///< ConnectRetry timer fired; re-attempt TCP connection.
    HOLD_TIMER_EXPIRES                                        = 10, ///< Hold timer expired; no KEEPALIVE or UPDATE received in time.
    KEEPALIVE_TIMER_EXPIRES                                   = 11, ///< Time to send an outbound KEEPALIVE.
    DELAY_OPEN_TIMER_EXPIRES                                  = 12, ///< DelayOpen timer expired; send OPEN now.
    IDLE_HOLD_TIMER_EXPIRES                                   = 13, ///< Idle-hold dampening interval elapsed; ready to reconnect.

    // TCP connection events (14–18)
    TCP_CONNECTION_VALID                                      = 14, ///< An inbound TCP connection arrived from a known peer address.
    TCP_CR_INVALID                                            = 15, ///< Inbound TCP from an unknown/invalid source.
    TCP_CR_ACKED                                              = 16, ///< Outbound TCP SYN-ACK received (active connection progressing).
    TCP_CONNECTION_CONFIRMED                                  = 17, ///< TCP connection fully established.
    TCP_CONNECTION_FAILS                                      = 18, ///< TCP connection attempt failed or was refused.

    // BGP message events (19–28)
    BGP_OPEN                                                  = 19, ///< Valid OPEN message received.
    BGP_OPEN_DELAY_OPEN_TIMER                                 = 20, ///< OPEN received while DelayOpen timer is running.
    BGP_HEADER_ERR                                            = 21, ///< Malformed BGP message header.
    BGP_OPEN_MSG_ERR                                          = 22, ///< Malformed or unacceptable OPEN message.
    OPEN_COLLISION_DUMP                                       = 23, ///< Collision resolution: this session loses and must be dropped.
    NOTIF_MSG_VER_ERR                                         = 24, ///< NOTIFICATION with Unsupported Version Number subcode received.
    NOTIF_MSG                                                 = 25, ///< Any other NOTIFICATION received.
    KEEPALIVE_MSG                                             = 26, ///< KEEPALIVE received.
    UPDATE_MSG                                                = 27, ///< Valid UPDATE received.
    UPDATE_MSG_ERR                                            = 28, ///< Malformed UPDATE received.

    // Extension events (not in RFC 4271 but used here)
    ROUTE_REFRESH                                             = 29, ///< ROUTE-REFRESH message received (RFC 2918 / RFC 7313).
    BFD_DOWN                                                  = 30, ///< BFD session to the peer went down.
    BFD_UP                                                    = 31, ///< BFD session to the peer came up.
    MAX_PREFIX_REACHED                                        = 32, ///< Inbound prefix count exceeded the configured maximum-prefix limit.
};

/**
 * @brief Carries the error code and optional data for a BGP NOTIFICATION message.
 * @ingroup BGP
 *
 * The `code` field packs both the error code (high byte) and the error subcode
 * (low byte) into a single `uint16_t` for convenient construction and comparison.
 */
struct Notification
{
    uint16_t code = 0;              ///< High byte = error code, low byte = subcode (see BGP_NOTIFICATION_* constants).
    std::vector<uint8_t> data = {}; ///< Optional diagnostic data appended after the error codes.
};

/**
 * @brief Combined Address Family Indicator and Subsequent AFI key.
 * @ingroup BGP
 *
 * Used as a map key and in BGP OPEN capability negotiation.  The `afi` and
 * `safi` values follow IANA assignments (e.g. AFI=1/SAFI=1 = IPv4 Unicast).
 *
 * `flatten()` packs both values into one `uint32_t` suitable for use as a
 * config array index or simple integer key.
 */
struct AfiSafi
{
    uint16_t afi  = 0; ///< Address Family Identifier (IANA).
    uint8_t  safi = 0; ///< Subsequent Address Family Identifier (IANA).

    /**
     * @brief Packs `afi` and `safi` into a single 32-bit word.
     *
     * Encoding: bits [15:0] = AFI, bits [23:16] = SAFI.  Used as a config
     * registry index and as a flat map key when `std::hash<AfiSafi>` is not
     * available.
     */
    uint32_t flatten() const
    {
        uint32_t flat = afi;
        flat |= uint32_t(safi) << 16;
        return flat;
    }

    constexpr bool operator==(const AfiSafi& other) const noexcept
    {
        return afi == other.afi && safi == other.safi;
    }

    constexpr bool operator!=(const AfiSafi& other) const noexcept
    {
        return !(*this == other);
    }
};

/**
 * @brief Composite key identifying a neighbor for a specific AFI/SAFI.
 * @ingroup BGP
 *
 * Used by per-AF neighbor lookups where the same IP address may participate
 * in multiple address families.
 */
struct NeighborKey
{
    types::IPAddress ipAddr; ///< Peer IP address.
    uint16_t afi;            ///< Address Family Identifier.
    uint8_t  safi;           ///< Subsequent AFI.

    bool operator==(const NeighborKey& other) const noexcept
    {
        return ipAddr == other.ipAddr &&
               afi == other.afi &&
               safi == other.safi;
    }
};

/**
 * @brief Composite key identifying a peer by Router ID and AFI/SAFI.
 * @ingroup BGP
 *
 * Used to map an established peer's Router ID (from the OPEN message) back to
 * the per-AF @ref NeighborAf entry.  Constructed from a @ref NeighborKey and
 * the peer's Router ID after session establishment.
 */
struct PeerKey
{
    PeerKey() = default;
    PeerKey(const NeighborKey& key, uint32_t rid)
        : rid(rid), afi(key.afi), safi(key.safi) {}

    uint32_t rid;    ///< Peer BGP Router ID (from OPEN message).
    uint16_t afi;    ///< Address Family Identifier.
    uint8_t  safi;   ///< Subsequent AFI.

    bool operator==(const PeerKey& other) const noexcept
    {
        return rid == other.rid &&
               afi == other.afi &&
               safi == other.safi;
    }
};

/**
 * @brief Subtype field for a ROUTE-REFRESH message as defined by RFC 7313.
 * @ingroup BGP
 *
 * Enhanced Route Refresh uses BORR/EORR to bracket the re-advertisement of all
 * routes for an AF, allowing the receiver to purge stale entries once EORR is
 * received.
 */
enum class RouteRefreshReason : uint8_t
{
    Normal = BGP_ROUTE_REFRESH_NORMAL, ///< RFC 2918 plain route refresh (no bracketing).
    Borr   = BGP_ROUTE_REFRESH_BORR,   ///< Begin-Of-Route-Refresh: mark current RIB entries stale.
    Eorr   = BGP_ROUTE_REFRESH_EORR,   ///< End-Of-Route-Refresh: purge entries not re-advertised since BORR.
};

/**
 * @brief Converts an @ref AfiSafi to the generic address-family enum.
 *
 * Returns `types::AddressFamily::NONE` for any AFI that is not IPv4 or IPv6.
 */
inline types::AddressFamily toAddressFamily(const AfiSafi& family) noexcept {
    if (family.afi == BGP_AFI_IPV4)
        return types::AddressFamily::IPv4;
    if (family.afi == BGP_AFI_IPV6)
        return types::AddressFamily::IPv6;
    return types::AddressFamily::NONE;
}

/// Returns the RFC 4271 name string for an FSM state (e.g. `"Established"`).
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

/// Returns the RFC 4271 name string for an FSM event (e.g. `"BGPOpen"`).
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
} // namespace routing

namespace std
{
template <>
struct hash<routing::bgp::AfiSafi>
{
    size_t operator()(const routing::bgp::AfiSafi& family) const noexcept
    {
        uint32_t v = (static_cast<uint32_t>(family.afi) << 8) | family.safi;
        return std::hash<uint32_t>{}(v);
    }
};

template <>
struct hash<routing::bgp::NeighborKey>
{
    size_t operator()(const routing::bgp::NeighborKey& k) const noexcept
    {
        size_t h1 = std::hash<types::IPAddress>{}(k.ipAddr);
        size_t h2 = std::hash<uint32_t>{}((static_cast<uint32_t>(k.afi) << 8) | k.safi);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

template <>
struct hash<routing::bgp::PeerKey>
{
    size_t operator()(const routing::bgp::PeerKey& k) const noexcept
    {
        size_t h1 = std::hash<uint32_t>{}(k.rid);
        size_t h2 = std::hash<uint32_t>{}((static_cast<uint32_t>(k.afi) << 8) | k.safi);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};
}

#endif // BGP_TYPES_HPP

