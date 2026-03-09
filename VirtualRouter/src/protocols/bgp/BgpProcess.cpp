// BgpProcess.cpp

#include <VirtualRouter.h>

#include "BgpProcess.h"
#include "bgp/neighbor/Neighbor.h"

namespace BGP
{
BgpProcess::BgpProcess(uint32_t as, VirtualRouter* vrf)
    : routingInstance(vrf),
      asNumber(as),
      scheduler(vrf->getControlScheduler().create()),
      ntable(*this),
      configs(vrf->getRegistry().create<Config::BgpRegistry>(vrf->getInstanceId()))
{
    vrf->getRegistry().ensure(configs->get<Config::Bgp::BGP_BASE>(), configs.getKey());

    TCP::ListenOptions opts;
    opts.onAccept = BgpProcess::onAcceptCallback;
    opts.onAcceptUser = this;
    opts.recvCallback = BgpProcess::onReceiveCallback;
    opts.recvUser = this;

    listener = vrf->getTcp().listen(
        TCP::TcpEndpoint{
            .address = IPAddress{},
            .port = 179
        },
        opts
    );
}

BgpProcess::~BgpProcess() = default;

Session* BgpProcess::findSession(TCP::ConnId cid)
{
    auto it = sessions.find(cid);
    return (it != sessions.end()) ? &it->second : nullptr;
}

void BgpProcess::startActiveSession(Neighbor& nbr)
{
    auto tempKey = reinterpret_cast<TCP::ConnId>(&nbr);
    auto [it, ok] = sessions.emplace(tempKey, nbr);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START);
}

void BgpProcess::startPassiveSession(Neighbor& nbr)
{
    auto tempKey = reinterpret_cast<TCP::ConnId>(&nbr);
    auto [it, ok] = sessions.emplace(tempKey, nbr);
    if (ok)
        it->second.postEvent(FsmEvent::MANUAL_START_PASSIVE_TCP);
}


void BgpProcess::onSessionEstablished(Session& session)
{
    const uint32_t rid = session.getPeerRid();
    Neighbor& nbr = session.getNeighbor();
    ntable.activatePeer(nbr.neighborAddress, rid);
    nbr.rid = rid;
    nbr.session = &session;
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

AddressFamilyVariant* BgpProcess::findAddressFamily(AfiSafi& afi)
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

void BgpProcess::onAcceptCallback(TCP::AcceptCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);

    // Look up the configured neighbor for the remote address.
    const IPAddress& nbrIp = ctx.key.remote.address;
    Neighbor* nbr = bgp->ntable.lookup(nbrIp);

    // Check if accepting a connection is allowed
    auto allowPassive = [&]() {
        auto& connMode = nbr->getConfigs().get<Config::BgpNeighborSession::TRANSPORT_CONNECTION_MODE>();
        return !(connMode.hasValue() && connMode.load() /*active = true*/);
    };

    // eBGP Neighbor IP must be in same subnet 
    auto check = [&]() {
        auto& cfgs = nbr->getConfigs();
        bool connectCheck = nbr->isEbgp() &&
            !cfgs.get<Config::BgpNeighborSession::DISABLE_CONNECTION_CHECK>().load() &&
            !cfgs.get<Config::BgpNeighborSession::EBGP_MULTIHOP>().load();

        if (nbrIp.isV6)
        {
            auto* route = bgp->routingInstance->getRib().lookup(readU128(nbrIp.raw));
            return route && connectCheck ? route->source == RouteSource::CONNECTED : true;
        }
        else
        {
            auto* route = bgp->routingInstance->getRib().lookup(readU32(nbrIp.raw));
            return route && connectCheck ? route->source == RouteSource::CONNECTED : true;
        }
    };

    if (!nbr || !allowPassive() || !check())
    {
        ctx.newConn.disconnect();
        return;
    }

    TCP::ConnId cid = ctx.newConn.getId();

    auto [it, ok] = bgp->sessions.emplace(cid, *nbr, bgp->scheduler.ref());
    it->second.acceptConnection(std::move(ctx.newConn));
}

void BgpProcess::onConnectCallback(TCP::ConnCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);
    Session* session = bgp->findSession(ctx.id);
    if (!session)
        return;

    if (ctx.ev.type == TCP::TcpEventType::CONNECTED)
        session->postEvent(FsmEvent::TCP_CR_ACKED);
    else
        session->postEvent(FsmEvent::TCP_CONNECTION_FAILS);
}

void BgpProcess::onReceiveCallback(TCP::RecvCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);
    Session* session = bgp->findSession(ctx.id);
    if (!session)
        return;

    session->handleIncoming(ctx.consumer);
}
}
