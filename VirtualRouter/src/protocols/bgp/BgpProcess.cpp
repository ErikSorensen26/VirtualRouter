// BgpProcess.cpp

#include <VirtualRouter.h>

#include "BgpProcess.h"
#include "bgp/neighbor/Neighbor.h"

namespace BGP
{
BgpProcess::BgpProcess(uint32_t as, VirtualRouter* vrf)
    : routingInstance(vrf),
      asNumber(as),
      transmission(*this),
      scheduler(vrf->getControlScheduler().create()),
      ntable(*this),
      configs(vrf->getRegistry().create<Config::BgpRegistry>(
          Config::generateBgpKey(vrf->getInstanceId(), as, AddressFamily::NONE)
      ))
{
    TCP::ListenOptions opts;
    opts.acceptConnCallback = onConnect;
    opts.acceptedConnUser = this;
    opts.recvCallback = onReceive;

    listener = vrf->getTcp().listen(
        TCP::TcpEndpoint{
            .address = IPAddress{},
            .port = 179
        },
        opts
    );
}

void BgpProcess::onConnect(TCP::ConnCallbackCtx& ctx) noexcept
{
    
}

void BgpProcess::onAccept(TCP::AcceptCallbackCtx& ctx) noexcept
{
    auto* bgp = static_cast<BgpProcess*>(ctx.user);
    TCP::ConnId connId = ctx.newConn.getId();
    Neighbor* nbr = bgp->ntable.lookup(ctx.key.remote.address);
    if (!nbr)
    {
        bgp->routingInstance->getTcp().close(connId);
        return;
    }
    bgp->connections.emplace(connId, *nbr, ctx.newConn);
}

void BgpProcess::onReceive(TCP::RecvCallbackCtx& ctx) noexcept
{

}
}
