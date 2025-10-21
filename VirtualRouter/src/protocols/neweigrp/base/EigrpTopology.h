// EigrpTopology.h

#ifndef EIGRP_TOPOLOGY_H
#define EIGRP_TOPOLOGY_H

#include <cstdint>
#include <IPAddress.hpp>
#include <EigrpTypes.hpp>
#include <TopologyTable.h>

namespace Protocol
{
class EigrpInterface;
class TopologyTable;
class Eigrp;

class EigrpTopology
{
public:
    EigrpTopology(Eigrp& base);

    void initialize()
    {
        topologyTable = new TopologyTable(base);
    }

    void shutdown()
    {
        if (topologyTable)
        {
            delete topologyTable;
            topologyTable = nullptr;
        }
    }

    /**
     * @brief Recalculates routes based on updated metrics and variance.
     *
     * Initiates a recalculation of the routing table to account for changes in route
     * metrics or variance settings, ensuring optimal route selection and load balancing.
     */
    void recalculateRoutes();

    /**
     * @brief Recalculates all metrics for all routes
     *
     * Recalculates metrics for all new routes, mostly used when
     * a new interface is added.
     */
    void recalculateRouteMetrics();

    /**
     * @brief Updates routes based on stub configuration.
     *
     * Applies or retracts route advertisements based on the current stub settings,
     * ensuring that only permitted route types are advertised to neighbors.
     */
    void updateStubRoutes();

    /**
     * @brief calculates updates the route metric, feasible distance, and reported distance.
     *
     * Calculates a new metric, feasuble distance, and reported distance for the route provided
     * 
     * @param localCost Local link cost calculated seperately in the 
     * @param route Eigrp route that needs a metric added.
     */
    void addRouteMetric(uint32_t localCost, RoutingTable::Eigrp* route);

    /**
     * @brief Updates the routing table with connected routes.
     *
     * Integrates connected network routes into the EIGRP routing table, allowing
     * EIGRP to advertise and route traffic through these directly connected networks.
     *
     * @param eigrpInterface Shared pointer to the EIGRP interface (optional).
     */
    void updateRoutingTableForConnected(EigrpInterface* eigrpInterface = nullptr);

    /**
     * @brief Notifies all EIGRP interfaces about routing changes.
     *
     * Broadcasts routing updates to all active EIGRP interfaces, informing neighbors
     * of new, updated, or removed routes to ensure consistent and synchronized routing
     * information across the network.
     *
     * @param changedRoutes Vector of routes that have changed.
     */
    void notifyRoutingChange(const std::vector<EigrpConfigs::RoutingUpdate>& changedRoutes);

    void clearTopologyTable();
    void resetRoutes();
    void rebuildTopology();
    void handleRouteFlap(const IPAddress& prefix);

    // Topology Table
    Protocol::TopologyTable* topologyTable = nullptr;
    Eigrp& base;
    RoutingTable& routingTable;

    // Lists
    std::unordered_map<IPPrefix, EigrpConfigs::ActiveRoute> outstandingReplies; ///< Map of outstanding query IDs to neighbor IPs and timer IDs.
};
}

#endif // EIGRP_TOPOLOGY_H
