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

/**
 * @class TopologyTable
 * @brief Manages the EIGRP topology table.
 *
 * The TopologyTable class maintains information about all known routes,
 * including their feasibility and successor statuses, based on information
 * received from neighbors. It supports adding, updating, and removing routes,
 * as well as determining the best available paths considering EIGRP's metric
 * calculations and variance settings.
 */
class TopologyTable {
public:

    /**
     * @struct RouteInfo
     * @brief Contains information about a specific route in the topology table.
     */
    struct RouteInfo {
        EigrpInterface* eigrpInterface; ///< Pointer the the interface this was learned on.
        uint32_t bandwidthMetric;
        uint32_t delayMetric;
        uint32_t feasibleDistance; ///< Feasible distance of the route.
        uint32_t reportedDistance; ///< Reported distance from the neighbor.
        uint8_t hopCount; ///< Number of hops to the destination.
        uint8_t adminDistance = 90; ///< Administrative distance.
        IPAddress nextHop; ///< Next hop IP address.
        bool isSuccessor = false; ///< Indicates if this route is a successor.
        bool isFeasibleSuccessor = false; ///< Indicates if this route is a feasible successor.
        bool notFeasible = false; ///< Indicates if this route is feasible or not.
        std::chrono::steady_clock::time_point lastUpdate; ///< Timestamp of the last update.
        RoutingTable::Eigrp::RouteType routeType;
    };

    /**
     * @struct TopologyEntry
     * @brief Represents an entry in the topology table for a specific destination.
     */
    struct TopologyEntry {
        IPAddress destination; ///< Destination network.
        uint8_t prefixLength; ///< Prefix length of the destination.
        std::unordered_map<IPAddress, RouteInfo> routesByNeighbor; ///< Routes learned from each neighbor.
        bool isActive; ///< Indicates if the route is active.

        // Timers for Active and Stuck-In-Active
        uint32_t activeTimerId = 0; ///< Timer ID for active routes.
        uint32_t StuckInActiveTimerId = 0; ///< Timer ID for stuck-in-active routes.

        uint32_t bestFD; ///< Best feasible distance for the route.

        std::vector<IPAddress> feasibleSuccessors; ///< List of feasible successor neighbors.
        std::vector<IPAddress> successors; ///< List of successor neighbors.
    };

    /**
     * @brief Constructs a TopologyTable instance.
     *
     * Initializes the TopologyTable, associating it with the given EIGRP process
     * to enable route management and synchronization with routing updates.
     *
     * @param process Pointer to the EIGRP process.
     */
    TopologyTable(Eigrp& process);

    ~TopologyTable();

    /**
     * @brief gathers all of the best routes in the routing table
     *
     * @param network Network that you want successors for.
     * @param mask Prefix length for the network you want successors for.
     * @return Returns a vector of all successors for a specific route.
     */
    std::vector<RoutingTable::Eigrp*> getSuccessorsForRoute(const IPAddress& network, uint8_t mask);

    /**
     * @brief Adds or updates a route in the topology table.
     *
     * Inserts a new route or updates an existing route in the topology table based
     * on information received from a neighbor, adjusting route metrics and statuses
     * as necessary.
     *
     * @param neighbor Reference to neighbors IP.
     * @param destination Destination network.
     * @param prefixLength Prefix length of the destination.
     * @param routeInfo Information about the route.
     */
    void addOrUpdateRoute(const IPAddress& neighborIp, const IPAddress& destination, uint8_t prefixLength, const RouteInfo& routeInfo);

    /**
     * @brief Removes all routes associated with a specific neighbor.
     *
     * Deletes all routes learned from the specified neighbor, ensuring that stale
     * or invalid routes are no longer present in the topology table.
     *
     * @param neighborIp IP address of the neighbor.
     */
    void erase(const IPAddress& neighborIp);

    /**
     * @brief Retrieves the topology entry for a specific route.
     *
     * Searches for and returns the topology entry associated with the given destination
     * network, facilitating detailed route inspections and modifications.
     *
     * @param prefix Destination prefix.
     * @param mask Mask of prefix.
     * @return Shared pointer to the TopologyEntry or nullptr if not found.
     */
    TopologyEntry* find(const IPPrefix& prefix);

    /**
     * @brief Removes a route from a specific neighbor for a specific destination.
     *
     * Deletes the entire topology entry for the given destination network,
     * effectively removing all associated routing information.
     *
     * @param prefix Destination prefix.
     * @param mask Mask of prefix.
     */
    bool removeRoute(const IPPrefix& prefix, const IPAddress& neighbor);

    /**
     * @brief Prunes stale routes that have not been updated within the threshold.
     *
     * Scans the topology table for routes that have not received updates within
     * a specified stale threshold and removes them to maintain an accurate and
     * efficient routing table.
     */
    void pruneExpired();

    /**
     * @brief Handles the removal of a neighbor by cleaning up associated routes.
     *
     * Executes cleanup procedures when a neighbor is removed, including
     * deleting routes learned from the neighbor and updating the topology table.
     *
     * @param neighborIp IP address of the neighbor being removed.
     */
    void handleNeighborDown(const IPAddress& neighborIp);

    /**
     * @brief Retrieves all topology entries.
     *
     * Provides access to the entire topology table, allowing for comprehensive
     * inspections, exports, or modifications of routing information.
     *
     * @return Reference to the map of topology entries.
     */
    std::unordered_map<IPPrefix, TopologyEntry*>& entries() { std::lock_guard<std::mutex> lock(tableMutex); return topologyEntries; }

    uint8_t staleThreshold = 15; ///< Threshold in seconds to consider a route stale.
    std::mutex tableMutex; ///< Mutex for synchronizing access to the topology table.

private:
    std::unordered_map<IPPrefix, TopologyEntry*> topologyEntries; ///< Map of destination networks to their topology entries.
    Eigrp& eigrpProcess; ///< Shared pointer to the EIGRP process.
};
}
inline bool isFeasibleSuccessor(RoutingTable::Eigrp* candidate, RoutingTable::Eigrp* currentSuccessor)
{
    if (!currentSuccessor) return true;
    return candidate->reportedDistance < currentSuccessor->feasibleDistance;
}

#endif // TOPOLOGY_TABLE_H
