// BgpProcess.cpp

#include <chrono>
#include <VirtualRouter.h>

#include "BgpProcess.h"
#include "bgp/neighbor/Neighbor.h"

namespace routing::bgp
{
BgpProcess::BgpProcess(uint32_t as, core::VirtualRouter* vrf)
    : routingInstance(vrf),
      asNumber(as),
      scheduler(vrf->getControlScheduler().create()),
      ntable(*this),
      configs(vrf->getRegistry().create<config::BgpRegistry>(vrf->getInstanceId()))
{
    vrf->getRegistry().emplace(configs->get<config::Bgp::BGP_BASE>());
    scheduleScan();

    transport::tcp::ListenOptions opts;
    opts.policy.pathMtuDiscovery = configs->get<config::Bgp::BGP_BASE>().local().get()
        .get<config::BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY>().load();
    opts.onAccept = BgpProcess::onAcceptCallback;
    opts.onAcceptUser = this;
    opts.recvCallback = BgpProcess::onReceiveCallback;
    opts.recvUser = this;

    listener = vrf->getTcp().listen(
        transport::tcp::TcpEndpoint{
            .address = types::IPAddress{},
            .port = 179
        },
        opts
    );
}

BgpProcess::~BgpProcess() = default;

Session* BgpProcess::findSession(const types::IPAddress& addr)
{
    auto it = sessions.find(addr);
    return (it != sessions.end()) ? &it->second : nullptr;
}

void BgpProcess::startActiveSession(Neighbor& nbr)
{
    if (nbr.getConfigs().get<config::BgpNeighborSession::SHUTDOWN>().load())
        return;

    auto [it, ok] = sessions.emplace(nbr.neighborAddress, nbr);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START);
}

void BgpProcess::startPassiveSession(Neighbor& nbr)
{
    if (nbr.getConfigs().get<config::BgpNeighborSession::SHUTDOWN>().load())
        return;

    auto [it, ok] = sessions.emplace(nbr.neighborAddress, nbr);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START_PASSIVE_TCP);
}

void BgpProcess::shutdownNeighbor(Neighbor& nbr)
{
    Session* session = findSession(nbr.neighborAddress);
    if (session)
        session->postEvent(FsmEvent::MANUAL_STOP);
}

void BgpProcess::unshutdownNeighbor(Neighbor& nbr)
{
    auto& cfgs = nbr.getConfigs();
    auto& connMode = cfgs.get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
    bool passive = connMode.hasValue() && !connMode.load();

    // If a session already exists (likely in IDLE after being shut down), restart it in place.
    auto it = sessions.find(nbr.neighborAddress);
    if (it != sessions.end())
    {
        it->second.postEvent(passive ? FsmEvent::MANUAL_START_PASSIVE_TCP : FsmEvent::MANUAL_START);
        return;
    }

    // No session yet — create one normally.
    if (passive)
        startPassiveSession(nbr);
    else
        startActiveSession(nbr);
}

void BgpProcess::onSessionEstablished(Session& session)
{
    const uint32_t rid = session.getPeerRid();
    Neighbor& nbr = session.getNeighbor();
    ntable.activatePeer(nbr.neighborAddress, rid);
    nbr.rid = rid;
    nbr.session = &session;

    auto doEstablish = [this](const types::IPAddress& peerAddr) {
        Session* s = findSession(peerAddr);
        if (!s || !s->established()) return;
        for (auto& [afi, afVariant] : addressFamilies)
        {
            if (!s->getNegotiated().activeFamilies.count(afi))
                continue;
            std::visit([&](auto& fam) { fam.onPeerEstablished(*s); }, afVariant);
        }
    };

    auto& delayField = getConfigs().get<config::Bgp::BGP_UPDATE_DELAY>();
    if (delayField.hasValue())
    {
        const types::IPAddress peerAddr = nbr.neighborAddress;
        const uint16_t delaySecs = delayField.load();
        scheduler.ref().postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(delaySecs),
            [doEstablish, peerAddr](uint32_t) mutable { doEstablish(peerAddr); });
    }
    else
    {
        doEstablish(nbr.neighborAddress);
    }
}

void BgpProcess::onSessionDown(Session& session)
{
    const uint32_t rid = session.getPeerRid();
    Neighbor& nbr = session.getNeighbor();

    nbr.session = nullptr;

    if (rid != 0)
    {
        for (auto& [_, af] : addressFamilies)
        {
            std::visit([rid](auto& fam) {
                fam.invalidatePeer(rid);
            }, af);
        }

        ntable.deactivatePeer(rid);
    }

    nbr.rid = 0;
}

AddressFamilyVariant* BgpProcess::findAddressFamily(const AfiSafi& afi)
{
    auto it = addressFamilies.find(afi);
    if (it == addressFamilies.end())
        return nullptr;
    return &it->second;
}

void BgpProcess::disableAddressFamily(AfiSafi& afi)
{
    addressFamilies.erase(afi);
}

void BgpProcess::onAcceptCallback(transport::tcp::AcceptCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);

    // Look up the configured neighbor for the remote address.
    const types::IPAddress& nbrIp = ctx.key.remote.address;
    Neighbor* nbr = bgp->ntable.lookup(nbrIp);

    if (!nbr && bgp->configs->get<config::Bgp::BGP_LISTEN>().load() && nbrIp.isIPv4())
    {
        std::string matchedGroup;
        bgp->configs->get<config::Bgp::BGP_LISTEN_RANGE>().withRead(
            [&](const std::vector<std::tuple<uint32_t, uint32_t, std::string>>& ranges)
            {
                uint32_t remoteV4 = nbrIp.v4();
                for (const auto& [netAddr, prefixLen, pgName] : ranges)
                {
                    if (prefixLen > 32) continue;
                    uint32_t mask = types::v4Mask(static_cast<uint8_t>(prefixLen));
                    if ((remoteV4 & mask) == (netAddr & mask))
                    {
                        matchedGroup = pgName;
                        break;
                    }
                }
            });

        if (!matchedGroup.empty())
            nbr = bgp->ntable.createDynamicNeighbor(nbrIp, matchedGroup);
    }

    // Check if accepting a connection is allowed
    auto allowPassive = [&]() {
        auto& connMode = nbr->getConfigs().get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
        return !(connMode.hasValue() && connMode.load() /*active = true*/);
    };

    // eBGP Neighbor IP must be in same subnet
    auto check = [&]() {
        auto& cfgs = nbr->getConfigs();
        bool connectCheck = nbr->isEbgp() &&
            !cfgs.get<config::BgpNeighborSession::DISABLE_CONNECTION_CHECK>().load() &&
            !cfgs.get<config::BgpNeighborSession::EBGP_MULTIHOP>().load();

        if (nbrIp.isIPv6())
        {
            auto* route = bgp->routingInstance->getRib().lookup(nbrIp.v6raw());
            return route && connectCheck ? route->source == core::RouteSource::CONNECTED : true;
        }
        else
        {
            auto* route = bgp->routingInstance->getRib().lookup(nbrIp.v4raw());
            return route && connectCheck ? route->source == core::RouteSource::CONNECTED : true;
        }
    };

    auto isShutdown = [&]() {
        return nbr->getConfigs().get<config::BgpNeighborSession::SHUTDOWN>().load();
    };

    // BGP_LISTEN_LIMIT caps the total number of concurrently accepted sessions.
    auto overLimit = [&]() {
        auto& limitField = bgp->configs->get<config::Bgp::BGP_LISTEN_LIMIT>();
        return limitField.hasValue() && bgp->sessions.size() >= limitField.load();
    };

    if (!nbr || isShutdown() || !allowPassive() || !check() || overLimit())
    {
        ctx.newConn.disconnect();
        return;
    }

    // If the base session has established multisession, stage the connection
    // until BgpRx parses the OPEN and calls activateSession with the family.
    if (nbr->session && nbr->session->getNegotiated().multiSess)
    {
        MultiSession* ms = nbr->session->getMultiSession();
        if (!ms)
        {
            ctx.newConn.disconnect();
            return;
        }
        ms->connections.emplace(ctx.newConn.getId(), std::move(ctx.newConn));
        return;
    }

    auto [it, ok] = bgp->sessions.emplace(nbrIp, *nbr);
    it->second.acceptConnection(std::move(ctx.newConn));
}

void BgpProcess::onConnectCallback(transport::tcp::ConnCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);
    Session* session = bgp->findSession(ctx.key.remote.address);
    if (!session)
        return;

    if (ctx.ev.type == transport::tcp::TcpEventType::CONNECTED)
        session->postEvent(FsmEvent::TCP_CR_ACKED);
    else
        session->postEvent(FsmEvent::TCP_CONNECTION_FAILS);
}

void BgpProcess::onReceiveCallback(transport::tcp::RecvCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);
    Session* session = bgp->findSession(ctx.key.remote.address);
    if (!session)
        return;

    // Multi-Session
    if (auto* mg = session->getMultiSession(); mg)
    {
        if (auto* ses = mg->findMultiSession(ctx.id))
        {
            ses->handleIncoming(ctx.consumer);
            return;
        }
    }

    session->handleIncoming(ctx.consumer);
}

void BgpProcess::scheduleScan()
{
    uint8_t secs = configs->get<config::Bgp::BGP_SCAN_TIME>().load();
    scheduler.ref().postAfter(
        std::chrono::steady_clock::now() + std::chrono::seconds(secs),
        [this](uint32_t) {
            for (auto& [afi, af] : addressFamilies)
                std::visit([](auto& fam) { fam.scan(); }, af);
            scheduleScan();
        });
}
} // namespace routing
