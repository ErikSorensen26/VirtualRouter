// OspfRouteManager.h

#ifndef OSPF_ROUTE_MANAGER_H
#define OSPF_ROUTE_MANAGER_H

#include <AddressFamily.hpp>
#include <OspfRoutingTable.h>
#include "SpfTypes.hpp"

struct IPAddress;

namespace OSPF
{
class OspfArea;

namespace RouteManager
{
    using NhCache = std::unordered_map<Vertex, std::vector<OspfNextHop>, VertexHash>;

    template<typename NetworkLsa, typename RouterLsa>
    std::vector<std::pair<IPPrefix, OspfPath>> deriveIntraAreaRouters(const SpfResult& spf, OspfArea& area);
}
}

#endif // OSPF_ROUTE_MANAGER_H
