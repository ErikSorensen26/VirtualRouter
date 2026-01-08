// OspfRouteManager.h

#ifndef OSPF_ROUTE_MANAGER_H
#define OSPF_ROUTE_MANAGER_H

#include <AddressFamily.hpp>
#include <optional>
#include "SpfTypes.hpp"

struct IPAddress;

namespace OSPF
{
class OspfArea;
class OspfProcess;
class LsdbTable;
class OspfRib;
struct OspfNextHop;

class RouteManager
{
    using NhCache = std::unordered_map<Vertex, std::vector<OspfNextHop>, VertexHash>;

    RouteManager(OspfProcess& process);

private:
    std::vector<OspfNextHop> computeNextHops(uint32_t area, const Vertex& v, const SpfResult& spf, NhCache& cache);

    template<typename NetworkLsa, typename RouterLsa>
    void deriveIntraAreaRouters(const SpfResult& spf, const OspfArea& area);

    void dedupe(std::vector<OspfNextHop>& hops);
    std::optional<OspfNextHop> resolveDirectNextHop(uint32_t area, const Vertex& v, const ParentRef& pref);

    OspfProcess& process;
    OspfRib& rib;
    AddressFamily af;
};
}

#endif // OSPF_ROUTE_MANAGER_H
