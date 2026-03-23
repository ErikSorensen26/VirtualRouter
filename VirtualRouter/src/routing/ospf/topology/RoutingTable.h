// OspfRoutingTable.h

#ifndef OSPF_ROUTING_TABLE_H
#define OSPF_ROUTING_TABLE_H

#include <cstdint>
#include <IPAddress.h>
#include <unordered_set>
#include "TopologyTypes.hpp"

namespace core { class RoutingTable; }

namespace routing::ospf
{
class OspfProcess;
class Area;

class OspfRib
{
    struct OspfDiscardKey;
public:
    OspfRib(OspfProcess& process);

    const OspfRoute* lookup(const types::IPPrefix& prefix) const;
    bool lpmLookup(const types::IPAddress& addr, uint32_t area) const;

    std::vector<OspfRouteChange> replaceArea(Area& area, const std::vector<std::pair<types::IPPrefix, OspfPath>>& paths);
    std::vector<OspfRouteChange> replaceRoute(Area& area, const std::pair<types::IPPrefix, std::optional<OspfPath>>& path);

    void replaceExternals(const std::vector<std::pair<types::IPPrefix, OspfPath>>& paths);
    void replaceExternal(const std::pair<types::IPPrefix, std::optional<OspfPath>>& path);

    void installDiscardRoute(const OspfDiscardKey& key, uint32_t cost, uint8_t ad);
    void withdrawDiscardRoute(const OspfDiscardKey& key);

    bool validateInterAreaSummaryEligibility(const types::IPPrefix& prefix) const;

    std::vector<OspfRouteChange> refreshIntraRangeSuppression(uint32_t areaId, const std::unordered_set<types::IPPrefix>& ranges);

    std::vector<std::pair<types::IPPrefix, OspfPath>> getIntraAreaRoutes(uint32_t area);

private:
    struct PrefixState
    {
        std::vector<OspfPath> canidates;
        OspfRoute selected;
        bool hasSelected{false};
    };

    bool globalRibContains(const types::IPPrefix& prefix) const;

    // All canidate paths per prefix;
    std::unordered_map<types::IPPrefix, PrefixState> prefixStates;

    // Trach which prefixes an area contributes to
    std::unordered_map<uint32_t, std::unordered_set<types::IPPrefix>> areaIndex;

    // Track which prefixes the process contributes to
    std::unordered_set<types::IPPrefix> processWide;

    struct OspfDiscardKey
    {
        types::IPPrefix prefix;
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
            size_t h = std::hash<types::IPPrefix>{}(k.prefix);
            if (k.areaId)
                h ^= std::hash<uint32_t>{}(*k.areaId) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // Discard routes derived from area ranges
    std::unordered_map<OspfDiscardKey, types::IPPrefix, OspfDiscardKeyHash> discardRoutes;

    OspfProcess& process;
    core::RoutingTable& rib;

private:
    struct RecomputeCtx
    {
        const uint32_t areaId;
        const std::unordered_set<types::IPPrefix>& ranges;
        OspfRouteChange intraChange{};
        OspfRouteChange interChange{};
        bool intraChanged{false};
        bool interChanged{false};
    };

    // Returns true if intra, otherwise false for inter
    bool recomputeLocked(const types::IPPrefix& prefix, types::AddressFamily af, uint32_t procId, RecomputeCtx* intraCtx = nullptr);
    std::vector<OspfRouteChange> recomputeLocked(const std::unordered_set<types::IPPrefix>& touched, uint32_t areaId, const std::unordered_set<types::IPPrefix>& ranges);
    void recomputeLocked(const std::unordered_set<types::IPPrefix>& touched);
};
} // namespace routing

#endif // OSPF_ROUTING_TABLE_H

