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
class OspfProcess;

struct ExtRec
{
    IPPrefix prefix{};
    IPAddress fwd{};
    uint8_t options{0};
    uint32_t asbrRid{0};
    uint32_t metric{0};
    bool isType2{false};
};

struct SelState
{
    bool set = false;
    bool isType2 = false;
    uint64_t installedCost = 0;  // For E2, this is Y; for E1, X+Y
    uint64_t tieX = 0;           // Only meaningful for E2
    uint8_t options = 0;
    std::vector<OspfNextHop> nextHops;
};

namespace RouteManager
{
    using NhCache = std::unordered_map<Vertex, std::vector<OspfNextHop>, VertexHash>;

    OspfPath makePath(std::optional<uint32_t> areaId, uint8_t options, uint8_t adminDistance, uint64_t cost, std::vector<OspfNextHop> nextHops, OspfRouteType type);

    template<typename Policy>
    void deriveIntraAreaRoutes(const SpfResult& spf, std::vector<std::pair<IPPrefix, OspfPath>>& pathList, OspfArea& area);

    template<typename Policy>
    std::pair<IPPrefix, std::optional<OspfPath>> deriveInterAreaNetwork(OspfArea& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body);
    template <typename Policy>
    void deriveInterAreaRouter(OspfArea& area, const LsaKey& key, const LsaHeader& header, const LsaBody& body);
    template<typename Policy>
    void deriveInterAreaRoutes(const SpfResult& spf, std::vector<std::pair<IPPrefix, OspfPath>>& pathList, OspfArea& area);
    
    template<typename Policy>
    std::pair<IPPrefix, std::optional<OspfPath>> deriveExternalRoute(OspfProcess& area, const LsaKey& key, const std::pair<LsaHeader, LsaBody>& rec);
    template<typename Policy>
    std::vector<std::pair<IPPrefix, OspfPath>> deriveExternalRoutes(OspfProcess& area);
}
}

#endif // OSPF_ROUTE_MANAGER_H
