// OspfRouteManager.h

#ifndef OSPF_ROUTE_MANAGER_H
#define OSPF_ROUTE_MANAGER_H

#include <AddressFamily.hpp>

#include "RoutingTable.h"
#include "ospf/spf/SpfTypes.hpp"
#include "ospf/database/LSDB.hpp"

struct IPAddress;

namespace OSPF
{
class Area;
class OspfProcess;

namespace RouteManager
{
    using NhCache = std::unordered_map<Vertex, std::vector<OspfNextHop>, VertexHash>;

    OspfPath makePath(std::optional<uint32_t> areaId, uint8_t options, uint8_t adminDistance, uint64_t cost, std::vector<OspfNextHop> nextHops, OspfRouteType type);

    template<typename Policy>
    void deriveIntraAreaRoutes(const SpfResult& spf, std::vector<std::pair<IPPrefix, OspfPath>>& pathList, Area& area);

    template<typename Policy>
    std::pair<IPPrefix, std::optional<OspfPath>> deriveInterAreaNetwork(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body);
    template <typename Policy>
    void deriveInterAreaRouter(Area& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body);
    template<typename Policy>
    void deriveInterAreaRoutes(const SpfResult& spf, std::vector<std::pair<IPPrefix, OspfPath>>& pathList, Area& area);
    
    template<typename Policy>
    std::pair<IPPrefix, std::optional<OspfPath>> deriveExternalRoute(OspfProcess& process, const LsaKey& key, const std::pair<LsaHeader, LsaBody>& rec);
    template<typename Policy>
    std::vector<std::pair<IPPrefix, OspfPath>> deriveExternalRoutes(OspfProcess& area);
}
}

#endif // OSPF_ROUTE_MANAGER_H
