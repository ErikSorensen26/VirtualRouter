// OspfRoutingTable.h

#ifndef OSPF_ROUTING_TABLE_H
#define OSPF_ROUTING_TABLE_H

#include <cstdint>
#include <IPAddress.hpp>
#include <unordered_set>
#include <shared_mutex>
#include <functional>

class RoutingTable;

namespace OSPF
{
class OspfProcess;
class OspfArea;

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

    friend bool operator==(const OspfNextHop& a, const OspfNextHop& b)
    {
        return a.nextHop == b.nextHop && a.interfaceId == b.interfaceId;
    }
};

struct OspfPath
{
    uint8_t options{0};
    uint32_t area;
    OspfRouteType type;
    uint64_t cost;
    uint8_t adminDistance;
    std::vector<OspfNextHop> nextHops;

    friend bool operator==(const OspfPath& a, const OspfPath& b)
    {
        return a.options == b.options && a.area == b.area &&
               a.type == b.type && a.cost == b.cost &&
               a.adminDistance == b.adminDistance &&
               a.nextHops == b.nextHops;
    }
};

struct OspfRoute
{
    IPPrefix prefix;
    uint8_t options;
    uint64_t cost;
    uint8_t adminDistance;
    OspfRouteType type;
    uint32_t area;
    std::vector<OspfPath> paths;
};

struct OspfRouteChange
{
    IPPrefix prefix;
    uint8_t options;
    uint64_t cost;
    bool isRemoval{false};
};

class OspfRib
{
public:
    OspfRib(OspfProcess& process);

    const OspfRoute* lookup(const IPPrefix& prefix) const;

    std::vector<OspfRouteChange> replaceArea(uint32_t areaId, const std::vector<std::pair<IPPrefix, OspfPath>>& paths);

private:

    // All canidate paths per prefix;
    std::unordered_map<IPPrefix, std::vector<OspfPath>> allPaths;

    // Selectd best routes;
    std::unordered_map<IPPrefix, OspfRoute> globalRoutes;

    // Trach which prefixes an area contributes to
    std::unordered_map<uint32_t, std::unordered_set<IPPrefix>> areaIndex;

    mutable std::shared_mutex mutex;

    OspfProcess& process;
    RoutingTable& rib;

private:
    std::vector<OspfRouteChange> recomputeLocked(const std::unordered_set<IPPrefix>& touched);

};
}

#endif // OSPF_ROUTING_TABLE_H
