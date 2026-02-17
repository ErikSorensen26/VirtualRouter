// BgpConnection.cpp

#include <VirtualRouter.h>

#include "BgpConnection.h"
#include "bgp/neighbor/Neighbor.h"
#include "bgp/BgpProcess.h"

namespace BGP
{
Connection::Connection(Neighbor& nbr, TCP::Connection& c) noexcept
    : neighbor(nbr),
      connection(std::move(c))
{
    sendOpen();
}

Connection::Connection(Neighbor& nbr) noexcept
    : neighbor(nbr),
      connection(nbr.getProcess().routingInstance->getTcp().connect(
          TCP::TcpEndpoint{IPAddress{}, 0},
          TCP::TcpEndpoint{nbr.neighborAddress, 179})
      )
{
    sendOpen();
}
}
