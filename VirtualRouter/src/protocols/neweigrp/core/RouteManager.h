// RouteManager.h

#ifndef EIGRP_ROUTE_MANAGER_H
#define EIGRP_ROUTE_MANAGER_H

#include <vector>
#include <cstdint>
#include <IPAddress.hpp>
#include <RoutingTable.h>
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
    void clearAll();
    void installConnected(const EigrpInterface* iface);
    void removeConnected(const EigrpInterface* iface);
    void removeInterface(const EigrpInterface* iface);
    void redistributed(const RouteInfo& routeInfo);
    void installSummary(const RouteInfo& routeInfo);

    void redistributeRoute(const uint8_t* destination, uint8_t mask, const uint16_t protocol);
    void withdrawRoute(const std::vector<IPPrefix>& withdraws); // no update sent
    bool synchronizeRoutes(const std::vector<TopologyEntry*>& entry, const std::vector<IPPrefix>& withdraws = {}, const std::vector<const RouteInfo*>& individuals = {});
    bool synchronizeRoute(const TopologyEntry& entry);

    void applyVariance(uint32_t bestFD, uint8_t variance, const std::vector<RouteInfo>& feasibleRoutes);
    bool exists(const IPPrefix& prefix);
    void setStuckInActive(const IPPrefix& prefix);
    std::vector<RoutingTable::Eigrp> snapshot() const;

private:
    Eigrp& base;
    RoutingTable& rib;
};
}


#endif // EIGRP_ROUTE_MANAGER_H
