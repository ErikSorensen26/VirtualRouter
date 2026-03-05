// Session.h

#ifndef BGP_SESSION_H
#define BGP_SESSION_H

#include <span>
#include <cstdint>
#include <unordered_set>

#include <ControlScheduler.h>

#include "tcp/Connection.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/session/Fsm.h"
#include "bgp/session/SessionTimers.h"
#include "configs/registry/router/BgpRegistry.h"

struct BgpHeader;
class PacketBuilder;

namespace BGP
{
class Neighbor;
class BgpProcess;

// Negotiated capability set
struct Capabilities
{
    // Multiprotocol extensions
    std::vector<AfiSafi> mpFamilies;

    // Route refresh
    bool routeRefresh = false;
    bool enhancedRouteRefresh = false;

    // 4-byte ASN
    bool asn32bit = false;
    uint32_t asn;

    // Extended message size
    bool extendedMessage = false;

    // Graceful restart
    struct GracefulRestartFamily
    {
        AfiSafi family;
        bool forwardingStatePreserved;
    };

    bool gracefulRestart = false;
    bool restarting = false;
    uint16_t restartTime = 0;
    std::vector<GracefulRestartFamily> gracefulFamilies;

    // Long-lived graceful restart
    struct LlgrFamily
    {
        AfiSafi family;
        uint32_t staleTime;
        uint8_t flags;
    };

    bool llgr = false;
    std::vector<LlgrFamily> llgrFamilies;

    // ADD-PATH
    struct AddPathFamily
    {
        AfiSafi family;
        uint8_t sendReceive;
    };

    bool addPath = false;
    std::vector<AddPathFamily> addPathFamilies;

    // Outbound route filtering
    struct OrfEntry
    {
        AfiSafi family;
        uint8_t orfType;
        uint8_t sendReceive;
    };

    bool outboundRouteFiltering = false;
    std::vector<OrfEntry> orfEntries;

    // Extended next-hop encoding
    struct ExtendedNextHop
    {
        AfiSafi family;
        uint16_t nextHopAfi;
    };

    bool extendedNextHop = false;
    std::vector<ExtendedNextHop> extendedNextHopEntries;

    // Multiple labels
    bool multipleLabels = false;
    std::vector<AfiSafi> labeledFamilies;

    // Route-target constraints
    bool routeTargetConstraint = false;
    std::vector<AfiSafi> RtConstraintFamily;

    // BGPsec
    bool bgpsec = false;
    std::vector<AfiSafi> bgpsecFamilies;

    // Helpers
    bool supportsFamily(const AfiSafi& fam) const noexcept
    {
        for (const auto& f : mpFamilies)
            if (f == fam) return true;
        return false;
    }

    bool addPathSend(const AfiSafi& fam) const noexcept
    {
        for (const auto& ap : addPathFamilies)
            if (ap.family == fam) return (ap.sendReceive & BGP_ADD_PATH_SEND) != 0;
        return false;
    }

    bool addPathReceive(const AfiSafi& fam) const noexcept
    {
        for (const auto& ap : addPathFamilies)
            if (ap.family == fam) return (ap.sendReceive & BGP_ADD_PATH_RECEIVE) != 0;
        return false;
    }
};

// Negotiated session result
struct NegotiatedCapabilities
{
    bool asn32bit = false;
    bool routeRefresh = false;
    bool enhancedRR = false;
    bool gracefulRestart = false;
    bool llgr = false;
    bool extendedMessage = false;
    bool addpath = false;
    std::vector<AfiSafi> activeFamilies;
    std::vector<Capabilities::AddPathFamily> addPathFamilies;
    std::vector<Capabilities::GracefulRestartFamily> grFamilies;
    std::vector<Capabilities::LlgrFamily> llgrFamilies;
};

/**
 * @class Session
 * @brief Manages one BGP session with a single peer.
 *
 * A Session owns:
 *   – The FSM  (Fsm)
 *   – Session timers  (SessionTimers)
 *   – Up to two TCP connections  (active=outbound, passive=inbound)
 *   – Capability state  (local / peer / negotiated)
 *   – Outbound UPDATE send queue
 *
 * All state mutations must be serialised through the ProcessQueue held by
 * the owning BgpProcess.
 */
class Session
{
public:
    Session(Neighbor& nbr, ProcessQueueRef queue) noexcept;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept = delete;
    Session& operator=(Session&&) noexcept = delete;

    ~Session();

    // TCP management
    void acceptConnection(TCP::Connection&& conn);
    void initiateConnection();
    void closeActiveConnection() noexcept;
    void closePassiveConnection() noexcept;
    void closeAllConnections() noexcept;

    void handleIncoming(TCP::RxConsumer& consumer);

    void postEvent(FsmEvent event);

    void onFsmTransition(FsmState from, FsmState to, FsmEvent trigger);

    // Outbound message builders
    void sendOpen();
    void sendKeepalive();
    void sendNotification(const Notification& notif);
    void sendNotification(uint16_t code);
    void sendRouteRefresh(const AfiSafi& family, uint8_t subtype = BGP_ROUTE_REFRESH_NORMAL);

    template <typename N>
    void sendUpdate(const ParsedUpdate<N>& update);

    bool onOpenReceived(std::span<const uint8_t> data, Notification& error);
    bool onKeepaliveReceived(std::span<const uint8_t> data, Notification& error);
    bool onUpdateReceived(std::span<const uint8_t> data, Notification& error);
    bool onNotificationReceived(std::span<const uint8_t> data, Notification& error);
    bool onRouteRefreshReceived(std::span<const uint8_t> data, Notification& error);

    // Capability state
    Capabilities& getLocalCaps() noexcept { return localCaps; }
    const Capabilities& getLocalCaps() const noexcept { return localCaps; }
    Capabilities& getPeerCaps() noexcept { return peerCaps; }
    const Capabilities& getPeerCaps() const noexcept { return peerCaps; }
    NegotiatedCapabilities& getNegotiated() noexcept { return negotiated; }
    const NegotiatedCapabilities& getNegotiated() const noexcept { return negotiated; }

    uint16_t holdTime = 180; // TODO: will add configured values here later
    uint16_t keepaliveInterval = 60;

    // Queries
    bool established() const noexcept { return fsm.getState() == FsmState::ESTABLISHED; }
    FsmState getFsmState() const noexcept { return fsm.getState(); }
    uint32_t getPeerRid() const noexcept { return peerRouterId; }
    bool isEbgp() const noexcept;
    bool isOutgoing() const noexcept { return activeConn.has_value(); }

    void setPeerRid(uint32_t rid) { peerRouterId = rid; }

    Neighbor& getNeighbor() noexcept { return neighbor; }
    const Neighbor& getNeighbor() const noexcept { return neighbor; }
    SessionTimers& getTimers() noexcept { return timers; }
    const Config::BgpBaseRegistry& getBaseConfig() const noexcept { return base; }

    TCP::Connection* getPrimaryConnection() noexcept { return primaryConn; }

    bool resolveCollision(uint32_t incomingPeerRid);
    bool negotiateCapabilities(Notification& error);

private:
    // Open processing helpers
    void buildLocalCapabilities();

    // References
    Neighbor& neighbor;
    Config::BgpBaseRegistry& base;
    ProcessQueueRef queue;

    // Protocol State
    Fsm fsm;
    SessionTimers timers;

    // TCP connections
    std::optional<TCP::Connection> activeConn; // Outbound
    std::optional<TCP::Connection> passiveConn; // Inbound
    TCP::Connection* primaryConn = nullptr;

    // Capability state
    Capabilities localCaps;
    Capabilities peerCaps;
    NegotiatedCapabilities negotiated;

    uint32_t peerRouterId = 0;
    std::vector<uint8_t> updateSentQueue;
};
}

#endif // BGP_TRANSMISSION_H
