// OspfRoutingTable.h

#ifndef OSPF_ROUTING_TABLE_H
#define OSPF_ROUTING_TABLE_H

#include <cstdint>
#include <IPAddress.hpp>
#include <unordered_set>
#include <shared_mutex>
#include "OspfTopologyTypes.hpp"

class RoutingTable;
enum class RouteSource : uint8_t;

namespace OSPF
{
class Topology;

class OspfRib
{
public:
    OspfRib(Topology& process);

    const OspfRoute* lookup(const IPPrefix& prefix) const;

    std::vector<OspfRouteChange> replaceArea(uint32_t areaId, const std::vector<std::pair<IPPrefix, OspfPath>>& paths);
    void replaceRoute(uint32_t areaId, const std::pair<IPPrefix, std::optional<OspfPath>>& path);

    void replaceExternals(const std::vector<std::pair<IPPrefix, OspfPath>>& paths);
    void replaceExternal(const std::pair<IPPrefix, std::optional<OspfPath>>& path);

private:
    struct PrefixState
    {
        std::vector<OspfPath> canidates;
        OspfRoute selected;
        bool hasSelected{false};
    };

    // All canidate paths per prefix;
    std::unordered_map<IPPrefix, PrefixState> prefixStates;

    // Trach which prefixes an area contributes to
    std::unordered_map<uint32_t, std::unordered_set<IPPrefix>> areaIndex;

    // Track which prefixes the process contributes to
    std::unordered_set<IPPrefix> processWide;

    mutable std::shared_mutex mutex;

    Topology& topology;
    RoutingTable& rib;

private:
    std::optional<OspfRouteChange> recomputeLocked(const IPPrefix& prefix);
    std::vector<OspfRouteChange> recomputeLocked(const std::unordered_set<IPPrefix>& touched);
};
}

#endif // OSPF_ROUTING_TABLE_H
