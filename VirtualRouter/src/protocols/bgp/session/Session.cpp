// Session.cpp

#include <VirtualRouter.h>

#include "Session.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Session::Session(Neighbor& nbr, TCP::Connection& c) noexcept
    : base(nbr.getConfigs().get<Config::BgpNeighbor::BGP_BASE>().local().get()),
      neighbor(nbr),
      connection(std::move(c))
{
    holdTime = base.get<Config::BgpBase::HOLDTIME>().load();
    neighbor.getProcess().getTransmission().sendOpen(*this);
}

Session::Session(Neighbor& nbr) noexcept
    : base(nbr.getConfigs().get<Config::BgpNeighbor::BGP_BASE>().local().get()),
      neighbor(nbr),
      connection(nbr.getProcess().routingInstance->getTcp().connect(
          TCP::TcpEndpoint{IPAddress{}, 0},
          TCP::TcpEndpoint{nbr.neighborAddress, 179})
      )
{
    holdTime = base.get<Config::BgpBase::HOLDTIME>().load();
    neighbor.getProcess().getTransmission().sendOpen(*this);
}
}
