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
      configs(vrf->getRegistry().create<Config::BgpRegistry>(
          Config::generateBgpKey(vrf->getInstanceId(), as, ::AddressFamily::NONE)
      ))
{
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
    return (it != sessions.end()) ? it->second.get() : nullptr;
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
    if (rid != 0)
        ntable.deactivatePeer(rid);
    nbr.rid = 0;
    nbr.session = nullptr;
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
    Neighbor* nbr = bgp->ntable.lookup(ctx.key.remote.address);
    if (!nbr)
    {
        ctx.newConn.disconnect();
        return;
    }

    TCP::ConnId cid = ctx.newConn.getId();

    auto [it, ok] = bgp->sessions.emplace(cid, std::make_unique<Session>(*nbr, bgp->scheduler.ref()));
    it->second->acceptConnection(std::move(ctx.newConn));
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
