// RouteManager.h

#ifndef EIGRP_ROUTE_MANAGER_H
#define EIGRP_ROUTE_MANAGER_H

#include <vector>
#include <cstdint>
#include <IPAddress.hpp>
#include <RoutingTable.hpp>
#include <EigrpConfig.h>
#include <EigrpTypes.hpp>
#include <TopologyTable.h>

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
    void withdrawRoutes(const std::vector<IPPrefix>& withdraws); // no update sent
    void synchronizeRoutes(const std::vector<TopologyEntry*>& entry, const std::vector<IPPrefix>& withdraws = {}, const std::vector<const RouteInfo*>& individuals = {});
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
        auto& entry = *entryPtr;
        auto bestIt = entry.routesByNeighbor.find(entry.bestNeighbor);
        if (entry.successors.empty() || bestIt == entry.routesByNeighbor.end())
        {
            withdrawRoute(entry.prefix);
            return nullptr;
        }
        RibEntry<AddrType> ribEntry;

        for (const auto& neighbor : entry.successors)
        {
            auto it = entry.routesByNeighbor.find(neighbor);
            if (it == entry.routesByNeighbor.end()) continue;

            ribEntry.addNextHop(
                af == AddressFamily::IPv4 ? neighbor.v4 : neighbor.v6,
                it->second.routeInfo.originInterface,
                1
            );
        }

        ribEntry.prefix = af == AddressFamily::IPv4
            ? bestIt->second.routeInfo.prefix.v4
            : bestIt->second.routeInfo.prefix.v6;
        ribEntry.length = bestIt->second.routeInfo.prefix.prefixLength;
        ribEntry.source = RouteSource::EIGRP;
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
