// Session.cpp

#include <VirtualRouter.h>

#include "Session.h"
#include "bgp/session/Collision.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"
#include "bgp/transport/BgpRx.h"
#include "bgp/transport/BgpTx.h"

namespace routing::bgp
{
Session::Session(Neighbor& nbr) noexcept
    : neighbor(nbr),
      base(nbr.getConfigs().get<config::BgpNeighborSession::BGP_BASE>().get()),
      fsm(*this),
      timers(*this)
{
    neighbor.buildAttributeRanges();
    holdTime = base.get<config::BgpTransportBase::HOLDTIME>().load();
    uint16_t cfgKa = base.get<config::BgpTransportBase::KEEPALIVE_INTERVAL>().load();
    keepaliveInterval = (cfgKa > 0 && cfgKa < holdTime) ? cfgKa : holdTime / 3;

    buildLocalCapabilities();

    fsm.setTransitionCallback(
        [this](FsmState from, FsmState to, FsmEvent trigger) {
            this->onFsmTransition(from, to, trigger);
        });
}

Session::Session(Neighbor& nbr, const AfiSafi& family) noexcept
    : Session(nbr)
{
    multiSession = family;
    localCaps.multiSessionFamilies = {family};
}

Session::~Session()
{
    if (std::holds_alternative<MultiSession>(multiSession))
        neighbor.session = nullptr;
    timers.cancelAll();
    closeAllConnections();
}

MultiSession* Session::getMultiSession()
{
    if (!established() || !localCaps.multiSess) return nullptr;
    return std::holds_alternative<MultiSession>(multiSession)
        ? &std::get<MultiSession>(multiSession) : nullptr;
}

AfiSafi* Session::getMultiSessionAfi()
{
    if (!established() || !localCaps.multiSess) return nullptr;
    return std::holds_alternative<AfiSafi>(multiSession)
        ? &std::get<AfiSafi>(multiSession) : nullptr;
}

void Session::startActiveMultiSession(const AfiSafi& family)
{
    if (!std::holds_alternative<MultiSession>(multiSession) ||
        !negotiated.multiSessionFamilies.contains(family))
        return;

    auto [it, ok] = std::get<MultiSession>(multiSession).sessions.try_emplace(family, neighbor, family);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START);
}

void Session::startPassiveMultiSession(const AfiSafi& family)
{
    if (!std::holds_alternative<MultiSession>(multiSession) ||
        !negotiated.multiSessionFamilies.contains(family))
        return;
    
    auto [it, ok] = std::get<MultiSession>(multiSession).sessions.try_emplace(family, neighbor, family);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START_PASSIVE_TCP);
}

void Session::buildLocalCapabilities()
{
    auto& procCfg = neighbor.getProcess().getConfigs();

    {
        auto& cfgs = neighbor.getConfigs();
        auto localAsField = cfgs.get<config::BgpNeighborSession::LOCAL_AS>();
        localCaps.asn = localAsField.hasValue() ? config::BgpLocalAs::as(localAsField.load()) : neighbor.getProcess().asNumber;
    }

    localCaps.asn32bit = true;

    localCaps.routeRefresh = true;
    localCaps.enhancedRouteRefresh = true;

    bool grEnabled = procCfg.get<config::Bgp::BGP_GRACEFUL_RESTART>().load();
    if (grEnabled)
    {
        localCaps.gracefulRestart = true;
        localCaps.restartTime = procCfg.get<config::Bgp::BGP_GRACEFUL_RESTART_TIME>().load();
    }

    localCaps.multiSess = neighbor.getConfigs().get<config::BgpNeighborSession::TRANSPORT_MULTI_SESSION>().load();
    localCaps.extendedMessage = true;
    localCaps.linkLocalNextHop = true;

    // ADD-PATH and ORF: advertise per-AF capabilities based on neighbor AF config.
    neighbor.getProcess().forEachAf([&](const AfiSafi& afi) {
        localCaps.mpFamilies.push_back(afi);

        auto& afNbrCfgs = neighbor.getAfNeighbor(afi).getConfigs();

        bool rx = afNbrCfgs.get<config::BgpAfBase::ADDITIONAL_PATHS_RECEIVE>().load();
        bool tx = afNbrCfgs.get<config::BgpAfBase::ADDITIONAL_PATHS_SEND>().load();
        uint8_t apSr = 0;
        if (rx) apSr |= BGP_ADD_PATH_RECEIVE;
        if (tx) apSr |= BGP_ADD_PATH_SEND;
        if (apSr)
        {
            localCaps.addPathFamilies.push_back({afi, apSr});
            localCaps.addPath = true;
        }

        bool orfBoth = afNbrCfgs.get<config::BgpNeighbor::ORF_BOTH>().load();
        bool orfRecv = afNbrCfgs.get<config::BgpNeighbor::ORF_RECEIVE>().load();
        bool orfSend = afNbrCfgs.get<config::BgpNeighbor::ORF_SEND>().load();
        uint8_t orfSr = orfBoth ? BGP_ORF_BOTH
                      : ((orfRecv ? BGP_ORF_RECEIVE : 0) | (orfSend ? BGP_ORF_SEND : 0));
        if (orfSr)
        {
            localCaps.orfEntries.push_back({afi, BGP_ORF_TYPE_PREFIX_LIST, orfSr});
            localCaps.outboundRouteFiltering = true;
        }
    });
}

void Session::acceptConnection(transport::tcp::Connection&& conn)
{
    passiveConn.emplace(std::move(conn));
    primaryConn = &passiveConn.value();
    postEvent(FsmEvent::TCP_CONNECTION_CONFIRMED);
}

void Session::initiateConnection()
{
    auto& proc = neighbor.getProcess();
    auto& tcp = proc.routingInstance->getTcp();

    transport::tcp::ConnectOptions opts;
    opts.policy.pathMtuDiscovery =
        base.get<config::BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY>().load();

    if (std::holds_alternative<AfiSafi>(multiSession))
    {
        // Child session: callbacks route directly to this Session
        opts.callback = Session::onConnectCallback;
        opts.callbackUser = this;
        opts.recvCallback = Session::onReceiveCallback;
        opts.recvUser = this;
    }
    else
    {
        // Base session: callbacks route through BgpProcess by remote address
        opts.callback = BgpProcess::onConnectCallback;
        opts.callbackUser = &proc;
        opts.recvCallback = BgpProcess::onReceiveCallback;
        opts.recvUser = &proc;
    }

    activeConn.emplace(tcp.connect(
        transport::tcp::TcpEndpoint{types::IPAddress{}, 0},
        transport::tcp::TcpEndpoint{neighbor.neighborAddress, 179},
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

void Session::closePassiveConnection() noexcept
{
    if (passiveConn.has_value())
    {
        auto& proc = neighbor.getProcess();
        auto& tcp = proc.routingInstance->getTcp();
        tcp.close(passiveConn->getId());
        passiveConn.reset();
    }
    primaryConn = activeConn.has_value() ? &activeConn.value() : nullptr;
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
    neighbor.getScheduler().post([this, event]() {
        fsm.processEvent(event);
    });
}

void Session::handleIncoming(transport::tcp::RxConsumer& consumer)
{
    BgpRx::handleIncoming(*this, consumer);
}

void Session::onFsmTransition(FsmState from, FsmState to, FsmEvent /*trigger*/)
{
    auto& proc = neighbor.getProcess();
    const bool isChild = std::holds_alternative<AfiSafi>(multiSession);

    if (to == FsmState::ESTABLISHED)
    {
        if (!isChild)
        {
            proc.onSessionEstablished(*this);

            if (negotiated.multiSess)
            {
                auto connectionMode = neighbor.getConfigs().get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
                bool passive = connectionMode.hasValue() && connectionMode.load() == config::bgp::BgpConnectionMode::PASSIVE;
                for (const auto& fam : negotiated.multiSessionFamilies)
                {
                    if (passive)
                        startPassiveMultiSession(fam);
                    else
                        startActiveMultiSession(fam);
                }
            }
        }
    }
    else if (from == FsmState::ESTABLISHED)
    {
        if (!isChild)
            proc.onSessionDown(*this);
    }
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
    if (notif.code == 0)
        return;
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

void Session::sendRouteRefresh(const AfiSafi& family, RouteRefreshReason reason)
{
    if (!primaryConn) return;
    BgpTx::buildRouteRefresh(*primaryConn, *this, family, reason);
    primaryConn->flush();
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
    return neighbor.isEbgp();
}

bool Session::isConfedEbgp() const noexcept
{
    return neighbor.isConfedEbgp();
}

bool Session::verifyConnection(uint64_t cid)
{
    return (activeConn.has_value() && activeConn->getId() == cid) ||
           (passiveConn.has_value() && passiveConn->getId() == cid);
}

bool Session::resolveCollision(uint32_t incomingPeerRid)
{
    uint32_t localRid = neighbor.getProcess().getRouterId();
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

void Session::onConnectCallback(transport::tcp::ConnCallbackCtx& ctx) noexcept
{
    auto* session = static_cast<Session*>(ctx.user);
    if (ctx.ev.type == transport::tcp::TcpEventType::CONNECTED)
        session->postEvent(FsmEvent::TCP_CR_ACKED);
    else
        session->postEvent(FsmEvent::TCP_CONNECTION_FAILS);
}

void Session::onReceiveCallback(transport::tcp::RecvCallbackCtx& ctx) noexcept
{
    auto* session = static_cast<Session*>(ctx.user);
    session->handleIncoming(ctx.consumer);
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
            negotiated.activeFamilies.insert(lf);
    }

    // IPv4 unicast
    if (peerCaps.mpFamilies.empty())
    {
        AfiSafi ipv4uni{BGP_AFI_IPV4, BGP_SAFI_UNICAST};
        bool found = false;
        for (const auto& f : negotiated.activeFamilies)
            if (f == ipv4uni) { found = true; break; }
        if (!found)
            negotiated.activeFamilies.insert(ipv4uni);
    }

    // Link-local next hop
    negotiated.linkLocalNextHop = localCaps.linkLocalNextHop && peerCaps.linkLocalNextHop;

    // MULTI-SESSION
    if (localCaps.multiSess)
    {
        for (const auto& lsf : localCaps.multiSessionFamilies)
        {
            for (const auto& psf : peerCaps.multiSessionFamilies)
            {
                if (lsf == psf)
                {
                    negotiated.multiSessionFamilies.insert(lsf);
                    negotiated.multiSess = true;
                    break;
                }
            }
        }
    }

    // Restrict active families to just this one
    if (std::holds_alternative<AfiSafi>(multiSession))
    {
        negotiated.activeFamilies.clear();
        if (peerCaps.supportsFamily(std::get<AfiSafi>(multiSession)))
            negotiated.activeFamilies.insert(std::get<AfiSafi>(multiSession));
    }

    // ADD-PATH: local SEND + peer RECEIVE → we send path IDs; local RECEIVE + peer SEND → we accept them.
    for (const auto& lap : localCaps.addPathFamilies)
    {
        for (const auto& pap : peerCaps.addPathFamilies)
        {
            if (lap.family == pap.family)
            {
                bool localSend = (lap.sendReceive & BGP_ADD_PATH_SEND) != 0;
                bool localRecv = (lap.sendReceive & BGP_ADD_PATH_RECEIVE) != 0;
                bool peerSend  = (pap.sendReceive & BGP_ADD_PATH_SEND) != 0;
                bool peerRecv  = (pap.sendReceive & BGP_ADD_PATH_RECEIVE) != 0;
                uint8_t agreed = 0;
                if (localSend && peerRecv) agreed |= BGP_ADD_PATH_SEND;
                if (localRecv && peerSend) agreed |= BGP_ADD_PATH_RECEIVE;
                if (agreed)
                {
                    negotiated.addPathFamilies.push_back({lap.family, agreed});
                    negotiated.addpath = true;
                }
                break;
            }
        }
    }

    // ORF
    for (const auto& loe : localCaps.orfEntries)
    {
        for (const auto& poe : peerCaps.orfEntries)
        {
            if (loe.family != poe.family || loe.orfType != poe.orfType)
                continue;
            uint8_t agreed = 0;
            if ((loe.sendReceive & BGP_ORF_SEND) && (poe.sendReceive & BGP_ORF_RECEIVE))
                agreed |= BGP_ORF_SEND;
            if ((loe.sendReceive & BGP_ORF_RECEIVE) && (poe.sendReceive & BGP_ORF_SEND))
                agreed |= BGP_ORF_RECEIVE;
            if (agreed)
            {
                negotiated.orfEntries.push_back({loe.family, loe.orfType, agreed});
                negotiated.orf = true;
            }
            break;
        }
    }

    // LLGR
    if (localCaps.llgr && peerCaps.llgr)
    {
        for (const auto& llf : localCaps.llgrFamilies)
        {
            for (const auto& plf : peerCaps.llgrFamilies)
            {
                if (llf.family == plf.family)
                {
                    negotiated.llgrFamilies.push_back({llf.family, std::min(llf.staleTime, plf.staleTime), static_cast<uint8_t>(llf.flags & plf.flags)});
                    negotiated.llgr = true;
                    break;
                }
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
} // namespace routing
