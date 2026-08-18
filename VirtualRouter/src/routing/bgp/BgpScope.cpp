// BgpScope.cpp

#include <chrono>
#include <variant>
#include <unordered_set>
#include <VirtualRouter.h>
#include <RCU.hpp>
#include "configs/registry/global/VrfRegistry.h"

#include "BgpProcess.h"
#include "BgpScope.h"
#include "bgp/neighbor/Neighbor.h"

namespace routing::bgp
{
BgpScope::BgpScope(BgpProcess& proc, core::VirtualRouter& vrf)
    : routingInstance(vrf),
      scheduler(vrf.getControlScheduler().create()),
      ntable(*this, proc.peerTemplates),
      process(proc)
{
    transport::tcp::ListenOptions opts;
    opts.policy.pathMtuDiscovery = process.configs.get<config::Bgp::BGP_BASE>().get()
        .get<config::BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY>().load();
    opts.onAccept = BgpScope::onAcceptCallback;
    opts.onAcceptUser = this;
    opts.recvCallback = BgpScope::onReceiveCallback;
    opts.recvUser = this;

    listener = vrf.getTcp().listen(
        transport::tcp::TcpEndpoint{
            .address = types::IPAddress{},
            .port = 179
        },
        opts
    );

    scheduleScan();
}

BgpScope::~BgpScope()
{
    process.configs.context().reset();

    scheduler.waitIdle();
    scheduler.release();

    sessions.clear();
    ntable.clear();
    addressFamilies.clear();
}

Session* BgpScope::findSession(const types::IPAddress& addr)
{
    auto it = sessions.find(addr);
    return (it != sessions.end()) ? &it->second : nullptr;
}

void BgpScope::startActiveSession(Neighbor& nbr)
{
    if (ntable.isShutdown(nbr))
        return;

    auto [it, ok] = sessions.try_emplace(nbr.neighborAddress, nbr, *this);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START);
}

void BgpScope::startPassiveSession(Neighbor& nbr)
{
    if (ntable.isShutdown(nbr))
        return;

    auto [it, ok] = sessions.try_emplace(nbr.neighborAddress, nbr, *this);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START_PASSIVE_TCP);
}

void BgpScope::shutdownNeighbor(Neighbor& nbr)
{
    Session* session = findSession(nbr.neighborAddress);
    if (session)
        session->postEvent(FsmEvent::MANUAL_STOP);
}

void BgpScope::unshutdownNeighbor(Neighbor& nbr)
{
    auto& cfgs = nbr.getConfigs();
    auto connMode = cfgs.get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
    bool passive = connMode.hasValue() && connMode.load() == config::bgp::BgpConnectionMode::PASSIVE;

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

void BgpScope::onSessionEstablished(Session& session)
{
    const uint32_t rid = session.getPeerRid();
    ntable.activatePeer(rid, session);

    auto doEstablish = [this](Session& s) {
        if (!s.established()) return;
        for (auto& [afi, afVariant] : addressFamilies)
        {
            if (!s.getNegotiated().activeFamilies.count(afi))
                continue;
            std::visit([&](auto& fam) { fam.onPeerEstablished(s); }, afVariant);
        }
    };

    auto delayField = process.configs.get<config::Bgp::BGP_UPDATE_DELAY>();
    if (delayField.hasValue())
    {
        const uint16_t delaySecs = delayField.load();
        const types::IPAddress peerAddr = session.neighbor.neighborAddress;
        scheduler.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(delaySecs),
            [this, doEstablish, peerAddr](uint32_t) {
                if (Session* s = findSession(peerAddr))
                    doEstablish(*s);
            });
    }
    else
    {
        doEstablish(session);
    }
}

void BgpScope::onSessionDown(Session& session)
{
    const uint32_t rid = session.getPeerRid();
    if (rid != 0)
    {
        for (auto& [_, af] : addressFamilies)
        {
            std::visit([rid](auto& fam) {
                fam.invalidatePeer(rid);
            }, af);
        }

        ntable.deactivatePeer(session);
    }
}

AddressFamilyVariant* BgpScope::findAddressFamily(const AfiSafi& afi)
{
    auto it = addressFamilies.find(afi);
    if (it == addressFamilies.end())
        return nullptr;
    return &it->second;
}

void BgpScope::disableAddressFamily(const AfiSafi& afi)
{
    addressFamilies.erase(afi);
}

void BgpScope::enableAddressFamily(const AfiSafi& af, config::BgpAddressFamilyRegistry& cfgs)
{
    [&]<typename... Ts>(std::variant<Ts...>*) {
        (([&] {
            if (af == Ts::afi && !addressFamilies.count(Ts::afi))
            {
                if (auto it = addressFamilies.find(af); it != addressFamilies.end())
                    return;
                addressFamilies.try_emplace(af, std::in_place_type<typename AddressFamily<Ts::afi>::type>, cfgs, *this, af);
            }
        }()), ...);
    }((Nlri*)nullptr);
}

void BgpScope::onAcceptCallback(transport::tcp::AcceptCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpScope*>(ctx.user);

    // Look up the configured neighbor for the remote address.
    const types::IPAddress& nbrIp = ctx.key.remote.address;
    Neighbor* nbr = bgp->ntable.lookup(nbrIp);

    if (!nbr && bgp->process.configs.get<config::Bgp::BGP_LISTEN>().load() && nbrIp.isIPv4())
    {
        std::string matchedGroup;
        uint32_t remoteV4 = nbrIp.v4();
        bgp->process.configs.get<config::Bgp::BGP_LISTEN_RANGE>().readEach(
            [&](const config::BgpListenRange& range)
            {
                const types::IPPrefix& prefix = range.prefix();
                if (prefix.prefixLength > 32) return false;
                uint32_t mask = types::v4Mask(static_cast<uint8_t>(prefix.prefixLength));
                if ((remoteV4 & mask) == (prefix.v4() & mask))
                {
                    matchedGroup = range.peerGroup();
                    return true;
                }
                return false;
            }
        );

        if (!matchedGroup.empty())
            nbr = bgp->ntable.createDynamicNeighbor(nbrIp, matchedGroup);
    }

    // Check if accepting a connection is allowed
    auto allowPassive = [&]() {
        auto connMode = nbr->getConfigs().get<config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
        return !(connMode.hasValue() && connMode.load() == config::bgp::BgpConnectionMode::ACTIVE);
    };

    auto check = [&]() {
        if (!bgp->ntable.isConnectionCheck(*nbr))
            return true;

        utils::RCU::Guard g;
        if (nbrIp.isIPv6())
        {
            const auto* route = bgp->routingInstance.getRib().lookup(nbrIp.v6raw(), g);
            return route && route->source == core::RouteSource::CONNECTED;
        }
        const auto* route = bgp->routingInstance.getRib().lookup(nbrIp.v4raw(), g);
        return route && route->source == core::RouteSource::CONNECTED;
    };

    // BGP_LISTEN_LIMIT caps the total number of concurrently accepted sessions.
    auto overLimit = [&]() {
        auto limitField = bgp->process.configs.get<config::Bgp::BGP_LISTEN_LIMIT>();
        return limitField.hasValue() && bgp->sessions.size() >= limitField.load();
    };

    if (!nbr || bgp->ntable.isShutdown(*nbr) || !allowPassive() || !check() || overLimit())
    {
        ctx.newConn.disconnect();
        return;
    }

    auto existing = bgp->sessions.find(nbrIp);
    if (existing != bgp->sessions.end() && existing->second.getNegotiated().multiSess)
    {
        MultiSession* ms = existing->second.getMultiSession();
        if (!ms)
        {
            ctx.newConn.disconnect();
            return;
        }
        ms->connections.emplace(ctx.newConn.getId(), std::move(ctx.newConn));
        return;
    }

    if (existing != bgp->sessions.end())
    {
        existing->second.acceptConnection(std::move(ctx.newConn));
        return;
    }

    auto [it, ok] = bgp->sessions.try_emplace(nbrIp, *nbr, *bgp);
    if (!ok)
    {
        ctx.newConn.disconnect();
        return;
    }
    it->second.acceptConnection(std::move(ctx.newConn));
}

void BgpScope::onConnectCallback(transport::tcp::ConnCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpScope*>(ctx.user);
    Session* session = bgp->findSession(ctx.key.remote.address);
    if (!session)
        return;

    if (ctx.ev.type == transport::tcp::TcpEventType::CONNECTED)
        session->postEvent(FsmEvent::TCP_CR_ACKED);
    else
        session->postEvent(FsmEvent::TCP_CONNECTION_FAILS);
}

void BgpScope::onReceiveCallback(transport::tcp::RecvCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpScope*>(ctx.user);
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

void BgpScope::scheduleScan()
{
    uint8_t secs = process.configs.get<config::Bgp::BGP_SCAN_TIME>().load();
    scheduler.postAfter(
        std::chrono::steady_clock::now() + std::chrono::seconds(secs),
        [this](uint32_t) {
            for (auto& [afi, af] : addressFamilies)
                std::visit([](auto& fam) { fam.scan(); }, af);
            scheduleScan();
        }
    );
}

const config::BgpRegistry& BgpScope::configs() const
{
    return process.configs;
}

AttributeManager& BgpScope::getAttrMgr()
{
    return process.attrMgr;
}
} // namespace routing
