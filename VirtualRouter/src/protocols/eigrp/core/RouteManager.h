// RouteManager.h

#ifndef EIGRP_ROUTE_MANAGER_H
#define EIGRP_ROUTE_MANAGER_H

#include <vector>
#include <cstdint>
#include <IPAddress.hpp>

#include "routing/RoutingTable.hpp"
#include "eigrp/core/EigrpConfig.h"
#include "eigrp/topology/TopologyTable.h"

namespace Eigrp
{
class Eigrp;
struct RouteInfo;
class EigrpInterface;

class RouteManager
{
public:
    explicit RouteManager(Eigrp& process);

    void withdrawRoute(const IPPrefix withdraws);
    void synchronizeRoutes(const std::vector<TopologyEntry*>& entry);
    void synchronizeRoute(const TopologyEntry& entry);


private:

    Eigrp& base;
    RoutingTable& rib;
    AddressFamily af;
    uint32_t as;

    template <typename AddrType>
    const RouteInfo* syncRoute(const TopologyEntry* entryPtr, uint8_t scale)
    {
        if (!entryPtr) return nullptr;
        std::optional<bool> isExternal{std::nullopt};
        auto& entry = *entryPtr;
        auto bestIt = entry.routesBySource.find(entry.bestNeighbor);
        if (entry.successors.empty() || bestIt == entry.routesBySource.end())
        {
            withdrawRoute(entry.prefix);
            return nullptr;
        }
        RibEntry<AddrType> ribEntry;

        for (const auto& neighbor : entry.successors)
        {
            auto it = entry.routesBySource.find(neighbor);
            if (it == entry.routesBySource.end()) continue;

            if (!isExternal.has_value())
            {
                isExternal = it->second.routeInfo.routeType == RouteType::EXTERNAL;
            }

            ribEntry.addNextHop(
                af == AddressFamily::IPv4 ? neighbor.v4() : neighbor.v6(),
                it->second.routeInfo.originInterface,
                1
            );
        }

        if constexpr (std::is_same_v<AddrType, uint32_t>)
        {
            ribEntry.prefix = bestIt->second.routeInfo.prefix.v4();
        }
        else
        {
            ribEntry.prefix = bestIt->second.routeInfo.prefix.v6();
        }

        ribEntry.length = bestIt->second.routeInfo.prefix.prefixLength;
        ribEntry.source = *isExternal ? RouteSource::EIGRP_EXTERNAL : RouteSource::EIGRP_INTERNAL;
        ribEntry.processId = as;
        ribEntry.adminDistance = bestIt->second.routeInfo.adminDistance;
        ribEntry.metric = bestIt->second.routeInfo.feasibleDistance * scale;

        return rib.addRoute<AddrType>(ribEntry)
            ? &bestIt->second
            : nullptr;
    }
};
}


#endif // EIGRP_ROUTE_MANAGER_H
