// Session.cpp

#include <VirtualRouter.h>

#include "Session.h"
#include "bgp/session/Collision.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"
#include "bgp/transport/BgpRx.h"
#include "bgp/transport/BgpTx.h"

namespace BGP
{
Session::Session(Neighbor& nbr, ProcessQueueRef queue) noexcept
    : neighbor(nbr),
      base(nbr.getConfigs().get<Config::BgpNeighborSession::BGP_BASE>().local().get()),
      queue(std::move(queue)),
      fsm(*this),
      timers(*this, this->queue)
{
    neighbor.session = this;

    holdTime = base.get<Config::BgpTransportBase::HOLDTIME>().load();
    keepaliveInterval = holdTime / 3;

    buildLocalCapabilities();

    // Register FSM transition callback
    fsm.setTransitionCallback(
        [this](FsmState from, FsmState to, FsmEvent trigger) {
            this->onFsmTransition(from, to, trigger);
        });
}

Session::~Session()
{
    neighbor.session = nullptr;
    timers.cancelAll();
    closeAllConnections();
}

void Session::buildLocalCapabilities()
{
    auto procCfg = neighbor.getProcess().getConfigs();

    localCaps.asn32bit = true;
    localCaps.asn = neighbor.getProcess().asNumber;

    localCaps.routeRefresh = true;
    localCaps.enhancedRouteRefresh = true;

    bool grEnabled = procCfg.get<Config::Bgp::BGP_GRACEFUL_RESTART>().load();
    if (grEnabled)
    {
        localCaps.gracefulRestart = true;
        localCaps.restartTime = procCfg.get<Config::Bgp::BGP_GRACEFUL_RESTART_RESTART_TIME>().load();
    }

    localCaps.extendedMessage = true;
}

void Session::acceptConnection(TCP::Connection&& conn)
{
    passiveConn.emplace(std::move(conn));
    primaryConn = &passiveConn.value();
    postEvent(FsmEvent::TCP_CONNECTION_CONFIRMED);
}

void Session::initiateConnection()
{
    auto& proc = neighbor.getProcess();
    auto& tcp = proc.routingInstance->getTcp();

    TCP::ConnectOptions opts;
    opts.callback = BgpProcess::onConnectCallback;
    opts.callbackUser = &proc;
    opts.recvCallback = BgpProcess::onReceiveCallback;
    opts.recvUser = &proc;

    activeConn.emplace(tcp.connect(
        TCP::TcpEndpoint{IPAddress{}, 0},
        TCP::TcpEndpoint{neighbor.neighborAddress, 179},
        opts
    ));

    if (activeConn->ok())
    {
        primaryConn = &activeConn.value();
    }
    else
    {
        activeConn.reset();
        postEvent(FsmEvent::TCP_CONNECTION_FAILS);
    }
}

void Session::closeActiveConnection() noexcept
{
    if (activeConn.has_value())
    {
        auto& proc = neighbor.getProcess();
        auto& tcp = proc.routingInstance->getTcp();
        tcp.close(activeConn->getId());
        activeConn.reset();
    }
    primaryConn = passiveConn.has_value() ? &passiveConn.value() : nullptr;
}

void Session::closeAllConnections() noexcept
{
    auto& proc = neighbor.getProcess();
    auto& tcp = proc.routingInstance->getTcp();

    if (activeConn.has_value())
    {
        tcp.close(activeConn->getId());
        activeConn.reset();
    }
    if (passiveConn.has_value())
    {
        tcp.close(passiveConn->getId());
        passiveConn.reset();
    }
    primaryConn = nullptr;
}

void Session::postEvent(FsmEvent event)
{
    queue.post([this, event]() {
        fsm.processEvent(event);
    });
}

void Session::handleIncoming(TCP::RxConsumer& consumer)
{
    auto& proc = neighbor.getProcess();
    proc.getTransmission().handleIncoming(*this, consumer);
}

void Session::onFsmTransition(FsmState from, FsmState to, FsmEvent /*trigger*/)
{
    auto& proc = neighbor.getProcess();

    if (to == FsmState::ESTABLISHED)
        proc.onSessionEstablished(*this);
    else if (from == FsmState::ESTABLISHED)
        proc.onSessionDown(*this);
}

void Session::sendOpen()
{
    if (primaryConn)
    {
        BgpTx::buildOpen(*primaryConn, *this);
        primaryConn->flush();
    }
}

void Session::sendKeepalive()
{
    if (primaryConn)
    {
        BgpTx::buildKeepalive(*primaryConn);
        primaryConn->flush();
    }
}

void Session::sendNotification(const Notification& notif)
{
    if (primaryConn)
    {
        BgpTx::buildNotification(*primaryConn, notif);
        primaryConn->flush();
    }
}

void Session::sendNotification(uint16_t code)
{
    Notification notif;
    notif.code = code;
    sendNotification(notif);
}

void Session::sendRouteRefresh(const AfiSafi& family, uint8_t subType)
{
    if (primaryConn)
    {
        BgpTx::buildRouteRefresh(*primaryConn, family, subType);
        primaryConn->flush();
    }
}

void Session::onOpenReceived()
{
    postEvent(FsmEvent::BGP_OPEN);
}

void Session::onKeepaliveReceived()
{
    postEvent(FsmEvent::KEEPALIVE_MSG);
}

void Session::onUpdateReceived()
{
    postEvent(FsmEvent::UPDATE_MSG);
}

void Session::onRouteRefreshReceived()
{
    postEvent(FsmEvent::ROUTE_REFRESH);
}

void Session::onNotificationReceived(std::span<const uint8_t> data)
{
    if (data.size() >= 2)
    {
        uint8_t code = data[0];
        uint8_t subcode = data[1];
        if (code == BGP_NOTIFICATION_OPEN &&
            subcode == BGP_GET_SUB_TYPE(BGP_NOTIFICATION_OPEN_UNSUPPORTED_VERSION))
        {
            postEvent(FsmEvent::NOTIF_MSG_VER_ERR);
        }
        else
        {
            postEvent(FsmEvent::NOTIF_MSG);
        }
    }
    else
    {
        postEvent(FsmEvent::NOTIF_MSG);
    }
}

bool Session::isEbgp() const noexcept
{
    auto& remAs = neighbor.getConfigs().get<Config::BgpNeighborSession::REMOTE_AS>();
    if (!remAs.hasValue()) return false;
    return remAs.load() != neighbor.getProcess().asNumber;
}

bool Session::resolveCollision(uint32_t incomingPeerRid)
{
    uint32_t localRid = neighbor.getProcess().rid;
    bool outgoing = activeConn.has_value();

    bool keep = CollisionDetector::shouldKeep(outgoing, localRid, incomingPeerRid);

    if (!keep)
    {
        if (outgoing)
            closeActiveConnection();
        else
            closePassiveConnection();
    }
    else
    {
        if (outgoing && passiveConn.has_value())
            closePassiveConnection();
        else if (!outgoing && activeConn.has_value())
            closeActiveConnection();
    }
    return keep;
}

void Session::negotiateCapabilities()
{
    negotiated = {};
    
    // 4-byte ASN
    negotiated.asn32bit = localCaps.asn32bit && peerCaps.asn32bit;

    // Route refresh
    negotiated.routeRefresh = localCaps.routeRefresh && peerCaps.routeRefresh;
    negotiated.enhancedRR = localCaps.enhancedRouteRefresh && peerCaps.enhancedRouteRefresh;

    // Extended message.
    negotiated.extendedMessage = localCaps.extendedMessage && peerCaps.extendedMessage;

    // Graceful restart.
    negotiated.gracefulRestart = localCaps.gracefulRestart && peerCaps.gracefulRestart;

    // Active families
    for (const auto& lf : localCaps.mpFamilies)
    {
        if (peerCaps.supportsFamily(lf))
            negotiated.activeFamilies.push_back(lf);
    }

    // IPv4 unicast
    if (peerCaps.mpFamilies.empty())
    {
        AfiSafi ipv4uni{BGP_AFI_IPV4, BGP_SAFI_UNICAST};
        bool found = false;
        for (const auto& f : negotiated.activeFamilies)
            if (f == ipv4uni) { found = true; break; }
        if (!found)
            negotiated.activeFamilies.push_back(ipv4uni);
    }

    // ADD-PATH
    for (const auto& lap : localCaps.addPathFamilies)
    {
        for (const auto& pap : peerCaps.addPathFamilies)
        {
            if (lap.family == pap.family)
            {
                // Only enable ADD-PATH for a family if both sides agree
                uint8_t agreed = lap.sendReceive & pap.sendReceive;
                if (agreed)
                {
                    negotiated.addPathFamilies.push_back({lap.family, agreed});
                    negotiated.addpath = true;
                }
                break;
            }
        }
    }

    // Graceful restart families.
    if (negotiated.gracefulRestart)
    {
        for (const auto& lgf : localCaps.gracefulFamilies)
        {
            for (const auto& pgf : peerCaps.gracefulFamilies)
            {
                if (lgf.family == pgf.family)
                {
                    negotiated.grFamilies.push_back(pgf);
                    break;
                }
            }
        }
    }
}
}
