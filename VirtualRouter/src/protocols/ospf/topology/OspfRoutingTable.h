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
class OspfProcess;
class OspfArea;

class OspfRib
{
    struct OspfDiscardKey;
public:
    OspfRib(OspfProcess& process);

    const OspfRoute* lookup(const IPPrefix& prefix) const;

    std::vector<OspfRouteChange> replaceArea(OspfArea& area, const std::vector<std::pair<IPPrefix, OspfPath>>& paths);
    void replaceRoute(uint32_t areaId, const std::pair<IPPrefix, std::optional<OspfPath>>& path);

    void replaceExternals(const std::vector<std::pair<IPPrefix, OspfPath>>& paths);
    void replaceExternal(const std::pair<IPPrefix, std::optional<OspfPath>>& path);

    void installDiscardRoute(const OspfDiscardKey& key, uint32_t cost, uint8_t ad);
    void withdrawDiscardRoute(const OspfDiscardKey& key);

    std::vector<OspfRouteChange> refreshIntraRangeSuppression(uint32_t areaId, const std::unordered_set<IPPrefix>& ranges);

    std::vector<std::pair<IPPrefix, OspfPath>> getIntraAreaRoutes(uint32_t area);

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

    struct OspfDiscardKey
    {
        IPPrefix prefix;
        std::optional<uint32_t> areaId;

        bool operator==(const OspfDiscardKey& o) const
        {
            return prefix == o.prefix && areaId == o.areaId;
        }
    };

    struct OspfDiscardKeyHash
    {
        size_t operator()(const OspfDiscardKey& k) const noexcept
        {
            size_t h = std::hash<IPPrefix>{}(k.prefix);
            if (k.areaId)
                h ^= std::hash<uint32_t>{}(*k.areaId) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // Discard routes derived from area ranges
    std::unordered_map<OspfDiscardKey, IPPrefix, OspfDiscardKeyHash> discardRoutes;

    mutable std::shared_mutex mutex;

    OspfProcess& process;
    RoutingTable& rib;

private:
    struct IntraRecomputeCtx
    {
        const uint32_t areaId;
        const std::unordered_set<IPPrefix>& ranges;
        OspfRouteChange* change{nullptr};
    };

    bool recomputeLocked(const IPPrefix& prefix, AddressFamily af, uint32_t procId, IntraRecomputeCtx* intraCtx = nullptr);
    std::vector<OspfRouteChange> recomputeLocked(const std::unordered_set<IPPrefix>& touched, uint32_t areaId, const std::unordered_set<IPPrefix>& ranges);
    void recomputeLocked(const std::unordered_set<IPPrefix>& touched);
};
}

#endif // OSPF_ROUTING_TABLE_H
