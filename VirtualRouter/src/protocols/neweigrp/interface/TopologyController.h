// TopologyController.h

#ifndef TOPOLOGY_CONTROLLER_H
#define TOPOLOGY_CONTROLLER_H

#include <cstdint>
#include <EigrpTypes.hpp>

namespace Protocol
{
class TopologyController
{
public:

    /**
     * @brief Flags a pending update for a specific route.
     *
     * Marks a route as pending an update, indicating that further actions or acknowledgements
     * are required before the route can be fully processed or advertised.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param route Route information.
     * @param neighborIp IP address of the neighbor.
     */
    void flagPendingUpdate(EigrpConfigs::NeighborInfo* neighbor, RoutingTable::Eigrp* route);

    /**
     * @brief Updates the routing table based on received routes.
     *
     * Integrates the received routes into the local routing table, recalculates metrics,
     * and determines the best paths based on EIGRP's metric calculations and policies.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param neighborIp Reference to neighbor's IP address.
     * @param routes Vector of received routes.
     * @param init Indicates if the update is part of initialization.
     * @param remove Indicates if routes are being removed.
     */
    void updateRoutingTable(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const std::vector<EigrpConfigs::RoutingUpdate>& routes);

    /**
     * @brief Updates the routing table for a specific destination.
     *
     * Re-evaluates and updates the routing entry for a single destination network,
     * ensuring that the best available route is selected and maintained.
     *
     * @param destination Destination network.
     */
    void updateRoutingTableForDestination(const IPPrefix& prefix);

    /**
     * @brief Handles updates related to Stub routing.
     *
     * Applies or retracts routes based on the current Stub configuration, ensuring that
     * only permitted routes are advertised or maintained.
     */
    void handleStubRouteUpdates();

    /**
     * @brief Handles the stuck in active for a local route
     *
     * Starts and handles the active route condition and timers for a certain destination.
     *
     * @param destination destination of the active route.
     */
    void enterActiveState(const IPAddress& destination);

    std::atomic<uint32_t> prefixCount = 0;
};
}

#endif // EIGRP_INTERFACE_TOPOLOGY_H
