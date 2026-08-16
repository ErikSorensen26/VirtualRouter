// Fsm.cpp

// BGP Finite State Machine
// All transitions are serialized through the Session's ProcessQueue

#include "Fsm.h"
#include "bgp/session/Session.h"
#include "bgp/session/SessionTimers.h"
#include "bgp/BgpScope.h"

namespace routing::bgp
{
static constexpr std::chrono::seconds kConnectRetryInterval{30};
static constexpr std::chrono::seconds kInitialHoldTime{240};

Fsm::Fsm(Session& session) noexcept
    : session(session)
{}

void Fsm::processEvent(FsmEvent event)
{
    switch (state)
    {
        case FsmState::IDLE:
            handleIdle(event);
            break;
        case FsmState::CONNECT:
            handleConnect(event);
            break;
        case FsmState::ACTIVE:
            handleActive(event);
            break;
        case FsmState::OPEN_SENT:
            handleOpenSent(event);
            break;
        case FsmState::OPEN_CONFIRMED:
            handleOpenConfirm(event);
            break;
        case FsmState::ESTABLISHED:
            handleEstablished(event);
            break;
    }
}

void Fsm::transitionTo(FsmState newState, FsmEvent trigger)
{
    FsmState from = state;
    state = newState;

    session.onFsmTransition(from, newState, trigger);

    if (transitionCallback)
        transitionCallback(from, newState, trigger);
}

void Fsm::resetToIdle(bool sendNotification, uint16_t notifCode, FsmEvent trigger)
{
    session.getTimers().cancelAll();
    session.getTimers().connectionRetryCount = 0;

    if (sendNotification)
        session.sendNotification(notifCode);

    session.closeAllConnections();
    transitionTo(FsmState::IDLE, trigger);
}

void Fsm::resetAndReconnect()
{
    session.getTimers().stopHoldTimer();
    session.getTimers().stopKeepaliveTimer();
    session.getTimers().connectionRetryCount++;
    session.closeAllConnections();

    auto count = session.getTimers().connectionRetryCount;
    auto base = kConnectRetryInterval;

    uint32_t shift = std::min<uint32_t>(count, 3);
    auto interval = std::chrono::seconds(base.count() << shift);

    if (interval > std::chrono::seconds(300))
        interval = std::chrono::seconds(300);

    session.getTimers().startConnectRetry(interval);
    if (!passiveMode)
        initiateOutgoingTcp();
}

void Fsm::initiateOutgoingTcp()
{
    session.initiateConnection();
}

bool Fsm::negotiateHoldTime()
{
    // RFC 4271 4.2: the negotiated hold time is the smaller of the two advertised
    // values. session.holdTime currently carries the peer's advertised value.
    auto& baseCfg = session.getBaseConfig();

    auto minHtCfg = baseCfg.get<config::BgpTransportBase::MINIMUM_HOLDTIME>();
    if (minHtCfg.hasValue() && session.holdTime != 0 && session.holdTime < minHtCfg.load())
    {
        resetToIdle(true, BGP_NOTIFICATION_OPEN_UNACCEPTABLE_HOLD, FsmEvent::BGP_OPEN_MSG_ERR);
        return false;
    }

    const uint16_t cfg   = baseCfg.get<config::BgpTransportBase::HOLDTIME>().load();
    const uint16_t minHt = std::min<uint16_t>(session.holdTime, cfg);
    session.holdTime = minHt;

    // Prefer the configured keepalive interval when it is shorter than holdtime/3.
    const uint16_t cfgKa = baseCfg.get<config::BgpTransportBase::KEEPALIVE_INTERVAL>().load();
    uint16_t kaInterval;
    if (minHt == 0)
        kaInterval = 0;
    else if (cfgKa > 0 && cfgKa < minHt)
        kaInterval = cfgKa;
    else
        kaInterval = minHt / 3;
    session.keepaliveInterval = kaInterval;

    if (minHt != 0)
    {
        session.getTimers().startHoldTimer(std::chrono::seconds(minHt));
        session.getTimers().startKeepaliveTimer(std::chrono::seconds(kaInterval));
    }
    else
    {
        // Hold time 0 disables both timers for the life of the session.
        session.getTimers().stopHoldTimer();
        session.getTimers().stopKeepaliveTimer();
    }
    return true;
}

void Fsm::handleIdle(FsmEvent event)
{
    switch (event)
    {
        case FsmEvent::MANUAL_START:
        case FsmEvent::AUTOMATIC_START:
        {
            session.initialize();
            passiveMode = false;
            session.getTimers().connectionRetryCount = 0;
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            initiateOutgoingTcp();
            transitionTo(FsmState::CONNECT, event);
            break;
        }
        case FsmEvent::MANUAL_START_PASSIVE_TCP:
        case FsmEvent::AUTOMATIC_START_PASSIVE_TCP:
        {
            session.initialize();
            passiveMode = true;
            session.getTimers().connectionRetryCount = 0;
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            transitionTo(FsmState::ACTIVE, event);
            break;
        }
        case FsmEvent::AUTOMATIC_START_DAMP:
        case FsmEvent::AUTOMATIC_START_DAMP_PASSIVE_TCP:
        {
            session.initialize();
            auto count = session.getTimers().connectionRetryCount;
            auto secs = std::chrono::seconds(5u << std::min<uint32_t>(count, 4));
            if (secs > std::chrono::seconds(120)) secs = std::chrono::seconds(120);
            session.getTimers().startIdleHoldTimer(secs);
            break;
        }
        case FsmEvent::IDLE_HOLD_TIMER_EXPIRES:
        {
            handleIdle(FsmEvent::MANUAL_START);
            break;
        }
        default:
        {
            break;
        }
    }
}

void Fsm::handleConnect(FsmEvent event)
{
    switch (event)
    {
        case FsmEvent::MANUAL_STOP:
        {
            session.getTimers().cancelAll();
            session.closeAllConnections();
            session.getTimers().connectionRetryCount = 0;
            transitionTo(FsmState::IDLE, event);
            break;
        }
        case FsmEvent::CONNECTION_RETRY_TIMER_EXPIRES:
        {
            // Drop current attempt, restart timer, try again.
            session.closeAllConnections();
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            initiateOutgoingTcp();
            // Stay in Connect.
            break;
        }
        case FsmEvent::TCP_CONNECTION_CONFIRMED:
        case FsmEvent::TCP_CR_ACKED:
        {
            // TCP established, send OPEN, start initial hold timer (RFC 4271 8.2.2).
            session.getTimers().stopConnectRetry();
            session.sendOpen();
            session.getTimers().startHoldTimer(kInitialHoldTime);
            transitionTo(FsmState::OPEN_SENT, event);
            break;
        }
        case FsmEvent::TCP_CONNECTION_FAILS:
        {
            // TCP failed, restart connect retry, listen for inbound.
            session.closeAllConnections();
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            transitionTo(FsmState::ACTIVE, event);
            break;
        }
        case FsmEvent::TCP_CONNECTION_VALID:
        {
            // Inbound connection arrived while outbound is in progress.
            // Track it via passiveConn; collision resolved when OPENs are exchanged.
            break;
        }
        case FsmEvent::TCP_CR_INVALID:
        {
            break;
        }
        case FsmEvent::BGP_OPEN_DELAY_OPEN_TIMER:
        case FsmEvent::BGP_OPEN:
        {
            // OPEN from inbound connection while also doing outbound.
            session.getTimers().stopConnectRetry();
            session.sendOpen();
            session.sendKeepalive();
            if (!negotiateHoldTime())
                break;
            transitionTo(FsmState::OPEN_CONFIRMED, event);
            break;
        }
        case FsmEvent::BGP_HEADER_ERR:
        case FsmEvent::BGP_OPEN_MSG_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_SENT, event);
            break;
        }
        case FsmEvent::AUTOMATIC_STOP:
        //case FsmEvent::MANUAL_STOP:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        default:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_SENT, event);
            break;
        }
    }
}

void Fsm::handleActive(FsmEvent event)
{
    switch (event)
    {
        case FsmEvent::MANUAL_STOP:
        {
            session.getTimers().cancelAll();
            session.closeAllConnections();
            session.getTimers().connectionRetryCount = 0;
            transitionTo(FsmState::IDLE, event);
            break;
        }
        case FsmEvent::CONNECTION_RETRY_TIMER_EXPIRES:
        {
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            initiateOutgoingTcp();
            transitionTo(FsmState::CONNECT, event);
            break;
        }
        case FsmEvent::TCP_CONNECTION_CONFIRMED:
        case FsmEvent::TCP_CR_ACKED:
        {
            session.getTimers().stopConnectRetry();
            session.sendOpen();
            session.getTimers().startHoldTimer(kInitialHoldTime);
            transitionTo(FsmState::OPEN_SENT, event);
            break;
        }
        case FsmEvent::TCP_CONNECTION_FAILS:
        {
            session.closeAllConnections();
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            break;
        }
        case FsmEvent::BGP_HEADER_ERR:
        case FsmEvent::BGP_OPEN_MSG_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_SENT, event);
            break;
        }
        case FsmEvent::AUTOMATIC_STOP:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        default:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_SENT, event);
            break;
        }
    }
}

void Fsm::handleOpenSent(FsmEvent event)
{
    switch (event)
    {
        case FsmEvent::MANUAL_STOP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_ADMIN_SHUT, event);
            break;
        }
        case FsmEvent::AUTOMATIC_STOP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_ADMIN_RESET, event);
            break;
        }
        case FsmEvent::HOLD_TIMER_EXPIRES:
        {
            resetToIdle(true, BGP_NOTIFICATION_HOLD_TIMER_EXPIRED, event);
            break;
        }
        case FsmEvent::TCP_CONNECTION_FAILS:
        {
            session.getTimers().stopHoldTimer();
            session.closeAllConnections();
            session.getTimers().startConnectRetry(kConnectRetryInterval);
            transitionTo(FsmState::ACTIVE, event);
            break;
        }
        case FsmEvent::BGP_OPEN:
        // Peer open received. The session has already validated content
        {
            session.getTimers().stopConnectRetry();
            session.sendKeepalive();
            if (!negotiateHoldTime())
                break;
            transitionTo(FsmState::OPEN_CONFIRMED, event);
            break;
        }
        case FsmEvent::OPEN_COLLISION_DUMP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION, event);
            break;
        }
        case FsmEvent::BGP_HEADER_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_HEADER_BAD_MESSAGE_TYPE, event);
            break;
        }
        case FsmEvent::BGP_OPEN_MSG_ERR:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        case FsmEvent::NOTIF_MSG_VER_ERR:
        case FsmEvent::NOTIF_MSG:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        case FsmEvent::KEEPALIVE_MSG:
        case FsmEvent::UPDATE_MSG_ERR:
        case FsmEvent::UPDATE_MSG:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_SENT, event);
            break;
        }
        default:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_SENT, event);
            break;
        }
    }
}

void Fsm::handleOpenConfirm(FsmEvent event)
{
    switch (event)
    {
        case FsmEvent::MANUAL_STOP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_ADMIN_SHUT, event);
            break;
        }
        case FsmEvent::AUTOMATIC_STOP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_ADMIN_RESET, event);
            break;
        }
        case FsmEvent::HOLD_TIMER_EXPIRES:
        {
            resetToIdle(true, BGP_NOTIFICATION_HOLD_TIMER_EXPIRED, event);
            break;
        }
        case FsmEvent::KEEPALIVE_TIMER_EXPIRES:
        {
            session.sendKeepalive();
            session.getTimers().restartKeepaliveTimer();
            break;
        }
        case FsmEvent::TCP_CONNECTION_FAILS:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        case FsmEvent::BGP_OPEN:
        {
            uint32_t peerRid = session.getPeerRid();
            if (!session.resolveCollision(peerRid))
            {
                resetToIdle(true, BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION, event);
            }
            break;
        }
        case FsmEvent::OPEN_COLLISION_DUMP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION, event);
            break;
        }
        case FsmEvent::BGP_HEADER_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_HEADER_BAD_MESSAGE_TYPE, event);
            break;
        }
        case FsmEvent::BGP_OPEN_MSG_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_OPEN_UNSUPPORTED_PARAMETER, event);
            break;
        }
        case FsmEvent::NOTIF_MSG_VER_ERR:
        case FsmEvent::NOTIF_MSG:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        case FsmEvent::KEEPALIVE_MSG:
        {
            if (session.holdTime != 0)
                session.getTimers().restartHoldTimer();
            session.getTimers().stopConnectRetry();
            transitionTo(FsmState::ESTABLISHED, event);
            break;
        }
        case FsmEvent::UPDATE_MSG:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_CONFIRM, event);
            break;
        }
        case FsmEvent::UPDATE_MSG_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST, event);
            break;
        }
        default:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_OPEN_CONFIRM, event);
            break;
        }
    }
}

void Fsm::handleEstablished(FsmEvent event)
{
    switch (event)
    {
        case FsmEvent::MANUAL_STOP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_ADMIN_SHUT, event);
            break;
        }
        case FsmEvent::AUTOMATIC_STOP:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_ADMIN_RESET, event);
            break;
        }
        case FsmEvent::HOLD_TIMER_EXPIRES:
        {
            resetToIdle(true, BGP_NOTIFICATION_HOLD_TIMER_EXPIRED, event);
            break;
        }
        case FsmEvent::KEEPALIVE_TIMER_EXPIRES:
        {
            session.sendKeepalive();
            session.getTimers().restartKeepaliveTimer();
            break;
        }
        case FsmEvent::TCP_CONNECTION_FAILS:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        case FsmEvent::BGP_OPEN:
        {
            uint32_t peerRid = session.getPeerRid();
            if (!session.resolveCollision(peerRid))
                resetToIdle(true, BGP_NOTIFICATION_CEASE_COLLISION_RESOLUTION, event);
            break;
        }
        case FsmEvent::BGP_HEADER_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_HEADER_BAD_MESSAGE_TYPE, event);
            break;
        }
        case FsmEvent::BGP_OPEN_MSG_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_FSM_ESTABLISH, event);
            break;
        }
        case FsmEvent::NOTIF_MSG_VER_ERR:
        case FsmEvent::NOTIF_MSG:
        {
            resetToIdle(false, BGP_NOTIFICATION_CEASE_UNSPECIFIC, event);
            break;
        }
        case FsmEvent::KEEPALIVE_MSG:
        {
            if (session.holdTime != 0)
                session.getTimers().restartHoldTimer();
            break;
        }
        case FsmEvent::UPDATE_MSG:
        {
            if (session.holdTime != 0)
                session.getTimers().restartHoldTimer();
            break;
        }
        case FsmEvent::UPDATE_MSG_ERR:
        {
            resetToIdle(true, BGP_NOTIFICATION_UPDATE_MALFORMED_ATTR_LIST, event);
            break;
        }
        case FsmEvent::ROUTE_REFRESH:
        {
            // Re-advertisement is triggered directly in BgpRx::processRouteRefresh.
            break;
        }
        case FsmEvent::BFD_DOWN:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_BFD_DOWN, event);
            break;
        }
        case FsmEvent::BFD_UP:
        {
            break;
        }
        case FsmEvent::MAX_PREFIX_REACHED:
        {
            resetToIdle(true, BGP_NOTIFICATION_CEASE_MAX_PREFIXES, event);
            break;
        }
        default:
        {
            break;
        }
    }
}
} // namespace routing
