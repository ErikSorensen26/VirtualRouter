// TopologyTable

#ifndef TOPOLOGY_TABLE_H
#define TOPOLOGY_TABLE_H

#include <cstdint>
#include <IPAddress.hpp>
#include <chrono>
#include <RoutingTable.h>

namespace Protocol
{
class EigrpInterface;
class Eigrp;

class DuelEngine {
public:
    DuelEngine(Eigrp& process);

    ~DuelEngine();

    /**
     * @brief Determines if a route is already advertised to all neighbors.
     *
     * Checks whether the specified route has been successfully advertised to all currently
     * known neighbors, preventing redundant advertisements and ensuring efficient network
     * utilization.
     *
     * @param network Network address.
     * @param mask Subnet mask.
     * @return True if the route is advertised to all neighbors, false otherwise.
     */
    bool isRouteAdvertised(const uint8_t* network, uint8_t mask);

    /**
     * @brief Updates dampening timestamps for the current route change.
     *
     * Updates tracking timestamps and counters for this AS if dampening is enabled and tracking
     */
    void onRouteConverged();

    /**
     * @brief Checks suppression status every couple of seconds.
     *
     * This is ran when dampening detects too many route changes in order
     * to supress changes to not overwhelm the system.
     */
    void checkSuppression();

    /**
     * @brief counts prefixes for route dampening.
     *
     * This is ran when a prefix is learned in order to cap a maximum amount of routes
     * if route dampening is enabled.
     */
    void handleRouteChange();

    /**
     * @brief Finds the best route for a given destination considering variance.
     *
     * Evaluates all available routes to a destination, considering EIGRP's
     * variance setting to allow unequal-cost load balancing, and identifies the
     * optimal route based on feasible distance and other metrics.
     *
     * @param prefix Destination prefix.
     * @param mask Mask of prefix.
     * @return Optional RouteInfo if a best route is found.
     */
    std::optional<RouteInfo> findBestRoute(const IPPrefix& prefix);

    /**
     * @brief Updates successors and feasible successors for a topology entry.
     *
     * Recalculates and assigns successor and feasible successor routes for the
     * specified topology entry, ensuring optimal route selection and redundancy.
     *
     * @param entry Reference to the TopologyEntry.
     */
    void recalculateFeasibleSuccessors(TopologyEntry* entry);

    /**
     * @brief Handles the failure of a route by removing it from the topology table.
     *
     * Removes the specified route from the topology table due to neighbor failure,
     * triggering route recalculations and potential advertisements to other neighbors.
     *
     * @param prefix Destination prefix.
     * @param mask Mask of prefix.
     * @param failedNeighborIp IP address of the failed neighbor.
     */
    void handleRouteFailure(const IPPrefix& prefix, const IPAddress& failedNeighborIp);

    /**
     * @brief Marks a route as passive, disabling further updates.
     *
     * Sets the specified route to a passive state, preventing it from being updated
     * or advertised further, often used during route maintenance or controlled shutdowns.
     *
     * @param prefix Destination prefix.
     * @param mask Mask of prefix.
     * @param eigrp Pointer to the EIGRP interface.
     */
    void markRouteAsPassive(const IPPrefix& prefix);
}

#endif // TOPOLOGY_TABLE_H
