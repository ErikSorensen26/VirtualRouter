// OspfRoutingTable.h

#ifndef OSPF_ROUTING_TABLE_H
#define OSPF_ROUTING_TABLE_H

#include <cstdint>
#include <IPAddress.hpp>
#include <unordered_set>
#include <shared_mutex>

namespace OSPF
{
enum class OspfRouteType : uint8_t
{
    INTRA_AREA = 0,
    INTER_AREA = 1,
    EXTERNAL = 2
};

struct OspfNextHop
{
    uint32_t interfaceId;
    IPAddress nextHop;
};

struct OspfPath
{
    uint32_t area;
    OspfRouteType type;
    uint64_t cost;
    std::vector<OspfNextHop> nextHops;
};

struct OspfRoute
{
    IPPrefix prefix;
    uint64_t cost;
    OspfRouteType type;
    uint32_t area;
    std::vector<OspfPath> paths;
};

class OspfRib
{
public:
    const OspfRoute* lookup(const IPPrefix& prefix) const;

    void replaceArea(uint32_t areaId, const std::vector<std::pair<IPPrefix, OspfPath>>& paths);

private:

    // All canidate paths per prefix;
    std::unordered_map<IPPrefix, std::vector<OspfPath>> allPaths;

    // Selectd best routes;
    std::unordered_map<IPPrefix, OspfRoute> globalRoutes;

    // Trach which prefixes an area contributes to
    std::unordered_map<uint32_t, std::unordered_set<IPPrefix>> areaIndex;

    mutable std::shared_mutex mutex;

private:
    void recomputeLocked(const IPPrefix& prefix);
};
}

#endif // OSPF_ROUTING_TABLE_H
