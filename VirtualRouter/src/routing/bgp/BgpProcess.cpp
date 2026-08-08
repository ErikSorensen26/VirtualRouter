// BgpProcess.cpp

#include <chrono>
#include <variant>
#include <vector>
#include <unordered_set>
#include <VirtualRouter.h>
#include <RCU.hpp>
#include "configs/registry/global/VrfRegistry.h"

#include "BgpProcess.h"
#include "bgp/neighbor/Neighbor.h"

namespace routing::bgp
{
BgpProcess::BgpProcess(uint32_t as, core::VirtualRouter* vrf)
    : asNumber(as),
      routingInstance(*vrf),
      scheduler(vrf->getControlScheduler().create()),
      ntable(*this),
      configs(vrf->getConfigs().get<config::Vrf::ROUTER_BGP>().get()),
      priv(*this)
{
    configs.get<config::Bgp::AUTONOMOUS_SYSTEM>().set(as);
    configs.context().set(this);
    priv.scheduleScan();

    transport::tcp::ListenOptions opts;
    opts.policy.pathMtuDiscovery = configs.get<config::Bgp::BGP_BASE>().get()
        .get<config::BgpTransportBase::TRANSPORT_PATH_MTU_DISCOVERY>().load();
    opts.onAccept = BgpProcess::onAcceptCallback;
    opts.onAcceptUser = this;
    opts.recvCallback = BgpProcess::onReceiveCallback;
    opts.recvUser = this;

    priv.listener = vrf->getTcp().listen(
        transport::tcp::TcpEndpoint{
            .address = types::IPAddress{},
            .port = 179
        },
        opts
    );
}

BgpProcess::~BgpProcess()
{
    configs.context().clear();
    scheduler.release();

    priv.sessions.clear();
    ntable.clear();
    priv.addressFamilies.clear();
}

BgpProcess::Private::Private(BgpProcess& proc)
    : rid([&proc]() {
          auto ridField = proc.configs.get<config::Bgp::BGP_ROUTER_ID>();
          if (ridField.hasValue())
              return ridField.load();
          uint32_t rid = 0;
          if (!proc.routingInstance.calculateRID(rid) || rid == 0)
              rid = proc.asNumber; // fall back to the AS number when no interface RID exists
          return rid;
      }()),
      process(proc)
{}

uint32_t BgpProcess::getRouterId() const
{
    return priv.rid;
}

void BgpProcess::enqueueSyncNeighbors()
{
    scheduler.post([this]() { ntable.syncNeighbors(); });
}

void BgpProcess::enqueueSyncAddressFamilies()
{
    scheduler.post([this]() {
        std::unordered_set<AfiSafi> configured;
        for (const auto& [key, _] : configs.get<config::Bgp::ADDRESS_FAMILIES>().get())
            configured.insert(AfiSafi{static_cast<uint16_t>(key & 0xFFFF),
                                      static_cast<uint8_t>((key >> 16) & 0xFF)});

        std::vector<AfiSafi> toDisable;
        for (const auto& [afi, _] : priv.addressFamilies)
            if (!configured.count(afi))
                toDisable.push_back(afi);
        for (AfiSafi afi : toDisable)
            disableAddressFamily(afi);

        [&]<typename... Ts>(std::variant<Ts...>*) {
            (([&] {
                if (configured.count(Ts::afi) && !priv.addressFamilies.count(Ts::afi))
                    enableAddressFamily<Ts::afi>();
            }()), ...);
        }((Nlri*)nullptr);
    });
}

void BgpProcess::enqueueMarkAllAfDirty(AfDirty category)
{
    scheduler.post([this, category]() {
        for (auto& [afi, af] : priv.addressFamilies)
            std::visit([category](auto& fam) { fam.markAfDirty(category); }, af);
    });
}

void BgpProcess::enqueueMarkAllInbound(InDirty category)
{
    scheduler.post([this, category]() {
        for (auto& [afi, af] : priv.addressFamilies)
            std::visit([category](auto& fam) { fam.markInboundDirty(category, 0); }, af);
    });
}

void BgpProcess::enqueueMarkAllOutbound(OutAttr attr)
{
    scheduler.post([this, attr]() {
        ntable.forEachNeighbor([&](Neighbor& nbr) {
            nbr.forEachAfNeighbor([&](NeighborAf& afNbr) { afNbr.markAttr(attr); });
        });
    });
}

void BgpProcess::enqueueRestartAllSessions()
{
    scheduler.post([this]() {
        ntable.forEachNeighbor([this](Neighbor& nbr) {
            ntable.restartNeighbor(nbr);
        });
    });
}

void BgpProcess::enqueueSyncConfederation()
{
    scheduler.post([this]() {
        ntable.forEachNeighbor([this](Neighbor& nbr) {
            nbr.syncClassification();
            ntable.restartNeighbor(nbr);
        });
    });
}

Session* BgpProcess::findSession(const types::IPAddress& addr)
{
    auto it = priv.sessions.find(addr);
    return (it != priv.sessions.end()) ? &it->second : nullptr;
}

void BgpProcess::startActiveSession(Neighbor& nbr)
{
    if (ntable.isShutdown(nbr))
        return;

    auto [it, ok] = priv.sessions.try_emplace(nbr.neighborAddress, nbr, *this);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START);
}

void BgpProcess::startPassiveSession(Neighbor& nbr)
{
    if (ntable.isShutdown(nbr))
        return;

    auto [it, ok] = priv.sessions.try_emplace(nbr.neighborAddress, nbr, *this);
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
    auto connMode = ntable.isTcpConnectionMode(nbr);
    bool passive = connMode.has_value() && !connMode.value();

    // If a session already exists (likely in IDLE after being shut down), restart it in place.
    auto it = priv.sessions.find(nbr.neighborAddress);
    if (it != priv.sessions.end())
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
    ntable.activatePeer(rid, session);

    auto doEstablish = [this](Session& s) {
        if (!s.established()) return;
        for (auto& [afi, afVariant] : priv.addressFamilies)
        {
            if (!s.getNegotiated().activeFamilies.count(afi))
                continue;
            std::visit([&](auto& fam) { fam.onPeerEstablished(s); }, afVariant);
        }
    };

    auto delayField = configs.get<config::Bgp::BGP_UPDATE_DELAY>();
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

void BgpProcess::onSessionDown(Session& session)
{
    const uint32_t rid = session.getPeerRid();
    if (rid != 0)
    {
        for (auto& [_, af] : priv.addressFamilies)
        {
            std::visit([rid](auto& fam) {
                fam.invalidatePeer(rid);
            }, af);
        }

        ntable.deactivatePeer(session);
    }
}

AddressFamilyVariant* BgpProcess::findAddressFamily(const AfiSafi& afi)
{
    auto it = priv.addressFamilies.find(afi);
    if (it == priv.addressFamilies.end())
        return nullptr;
    return &it->second;
}

void BgpProcess::disableAddressFamily(AfiSafi& afi)
{
    priv.addressFamilies.erase(afi);
}

void BgpProcess::onAcceptCallback(transport::tcp::AcceptCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);

    // Look up the configured neighbor for the remote address.
    const types::IPAddress& nbrIp = ctx.key.remote.address;
    Neighbor* nbr = bgp->ntable.lookup(nbrIp);

    if (!nbr && bgp->configs.get<config::Bgp::BGP_LISTEN>().load() && nbrIp.isIPv4())
    {
        std::string matchedGroup;
        bgp->configs.get<config::Bgp::BGP_LISTEN_RANGE>().withRead(
            [&](const auto& rangesList)
            {
                uint32_t remoteV4 = nbrIp.v4();
                for (const auto& [netAddr, prefixLen, pgName] : rangesList)
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
        auto connMode = bgp->ntable.isTcpConnectionMode(*nbr);
        return !(connMode.has_value() && connMode.value() /*active = true*/);
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
        auto limitField = bgp->configs.get<config::Bgp::BGP_LISTEN_LIMIT>();
        return limitField.hasValue() && bgp->priv.sessions.size() >= limitField.load();
    };

    if (!nbr || bgp->ntable.isShutdown(*nbr) || !allowPassive() || !check() || overLimit())
    {
        ctx.newConn.disconnect();
        return;
    }

    auto existing = bgp->priv.sessions.find(nbrIp);
    if (existing != bgp->priv.sessions.end() && existing->second.getNegotiated().multiSess)
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

    if (existing != bgp->priv.sessions.end())
    {
        existing->second.acceptConnection(std::move(ctx.newConn));
        return;
    }

    auto [it, ok] = bgp->priv.sessions.try_emplace(nbrIp, *nbr, *bgp);
    if (!ok)
    {
        ctx.newConn.disconnect();
        return;
    }
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

void BgpProcess::Private::scheduleScan()
{
    uint8_t secs = process.configs.get<config::Bgp::BGP_SCAN_TIME>().load();
    process.scheduler.postAfter(
        std::chrono::steady_clock::now() + std::chrono::seconds(secs),
        [this](uint32_t) {
            for (auto& [afi, af] : addressFamilies)
                std::visit([](auto& fam) { fam.scan(); }, af);
            scheduleScan();
        });
}
} // namespace routing
