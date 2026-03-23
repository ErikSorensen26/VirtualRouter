// Session.h

#ifndef BGP_SESSION_H
#define BGP_SESSION_H

#include <span>
#include <cstdint>
#include <optional>

#include "tcp/Connection.h"
#include "bgp/BgpTypes.hpp"
#include "bgp/rib/RibTypes.hpp"
#include "bgp/session/Fsm.h"
#include "bgp/session/SessionTimers.h"
#include "configs/registry/router/BgpRegistry.h"
#include "Capabilities.hpp"
#include "MultiSession.h"

namespace processing { class PacketBuilder; }

namespace routing::bgp
{
class Neighbor;
class BgpProcess;
class MultiSession;

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
    Session(Neighbor& nbr) noexcept;
    Session(Neighbor& nbr, const AfiSafi& afi) noexcept;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept = delete;
    Session& operator=(Session&&) noexcept = delete;

    ~Session();

    // TCP management
    void acceptConnection(transport::tcp::Connection&& conn);
    void initiateConnection();
    void closeActiveConnection() noexcept;
    void closePassiveConnection() noexcept;
    void closeAllConnections() noexcept;

    // MultiSession
    MultiSession* getMultiSession();
    AfiSafi* getMultiSessionAfi();
    void startActiveMultiSession(const AfiSafi& family);
    void startPassiveMultiSession(const AfiSafi& family);

    void handleIncoming(transport::tcp::RxConsumer& consumer);

    void postEvent(FsmEvent event);

    void onFsmTransition(FsmState from, FsmState to, FsmEvent trigger);

    // Outbound message builders
    void sendOpen();
    void sendKeepalive();
    void sendNotification(const Notification& notif);
    void sendNotification(uint16_t code);
    void sendRouteRefresh(const AfiSafi& family, RouteRefreshReason reason = RouteRefreshReason::Normal);

    template <typename N>
    void sendUpdate(const BuildUpdate<typename N::Nlri>& update);

    void onOpenReceived();
    void onKeepaliveReceived();
    void onUpdateReceived();
    void onRouteRefreshReceived();
    void onNotificationReceived(std::span<const uint8_t> data);

    // Capability state
    Capabilities& getLocalCaps() noexcept { return localCaps; }
    const Capabilities& getLocalCaps() const noexcept { return localCaps; }
    Capabilities& getPeerCaps() noexcept { return peerCaps; }
    const Capabilities& getPeerCaps() const noexcept { return peerCaps; }
    NegotiatedCapabilities& getNegotiated() noexcept { return negotiated; }
    const NegotiatedCapabilities& getNegotiated() const noexcept { return negotiated; }

    uint16_t holdTime = 180;
    uint16_t keepaliveInterval = 60;

    // Queries
    bool established() const noexcept { return fsm.getState() == FsmState::ESTABLISHED; }
    FsmState getFsmState() const noexcept { return fsm.getState(); }
    uint32_t getPeerRid() const noexcept { return peerRouterId; }
    bool isEbgp() const noexcept;
    bool isConfedEbgp() const noexcept;
    bool isOutgoing() const noexcept { return activeConn.has_value(); }

    void setPeerRid(uint32_t rid) { peerRouterId = rid; }

    Neighbor& getNeighbor() noexcept { return neighbor; }
    const Neighbor& getNeighbor() const noexcept { return neighbor; }
    SessionTimers& getTimers() noexcept { return timers; }
    const config::BgpBaseRegistry& getBaseConfig() const noexcept { return base; }

    transport::tcp::Connection* getPrimaryConnection() noexcept { return primaryConn; }
    const transport::tcp::Connection* getPrimaryConnection() const noexcept { return primaryConn; }

    bool verifyConnection(uint64_t cid);

    bool resolveCollision(uint32_t incomingPeerRid);
    void negotiateCapabilities();

    static void onConnectCallback(transport::tcp::ConnCallbackCtx& ctx) noexcept;
    static void onReceiveCallback(transport::tcp::RecvCallbackCtx& ctx) noexcept;

private:
    // Open processing helpers
    void buildLocalCapabilities();

    // References
    Neighbor& neighbor;
    config::BgpBaseRegistry& base;

    // Protocol State
    Fsm fsm;
    SessionTimers timers;

    // TCP connections
    std::optional<transport::tcp::Connection> activeConn; // Outbound
    std::optional<transport::tcp::Connection> passiveConn; // Inbound
    transport::tcp::Connection* primaryConn = nullptr;

    std::variant<AfiSafi, MultiSession> multiSession{std::in_place_type<MultiSession>};

    // Capability state
    Capabilities localCaps;
    Capabilities peerCaps;
    NegotiatedCapabilities negotiated;

    uint32_t peerRouterId = 0;
    std::vector<uint8_t> updateSentQueue;
};
} // namespace routing

#endif // BGP_SESSION_H

