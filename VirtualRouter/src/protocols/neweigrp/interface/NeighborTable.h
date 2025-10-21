// NeighborTable.h

#ifndef NEIGHBOR_TABLE
#define NEIGHBOR_TABLE

#include <shared_mutex>
#include <unordered_map>

struct IPAddress;
namespace EigrpConfigs
{
struct NeighborInfo;
struct NeighborState;
}

namespace Protocol
{
class NeighborTable
{

    /**
     * @brief Changes the state of a neighbor during initialization.
     *
     * Transitions the neighbor's state machine to a new state, ensuring proper synchronization
     * and handling of any necessary actions during the state change.
     *
     * @param neighbor pointer to the neighbor's information.
     * @param newState New state to transition to.
     */
    void setState(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, EigrpConfigs::NeighborState newState);

    /**
     * @brief Adds a neighbor to the EIGRP interface.
     *
     * Registers a new neighbor with the specified IP and MAC addresses, setting the
     * communication mode and initializing necessary state information.
     *
     * @param ipAddress IP address of the neighbor.
     * @param macAddress MAC address of the neighbor.
     * @param unicast Indicating whether the neighbor is unicast or multicast.
     */
    void createNeighbor(const IPAddress& ipAddress, const uint8_t* macAddress, bool unicast = false);

    /**
     * @brief Adds a unicast neighbor to the specified interface.
     *
     * Adds a unicast neighbor to a specified interface. Will convert a
     * multicast neighbor to a unicast neighbor if needed.
     *
     * @param neighborIp IP of the static neighbor.
     * @param interfaceType The type of interface this neighbor is being added to.
     * @param id The interface ID of the interface that this neighbor is being added to.
     */
    void createNeighbor(const IPAddress& neighborIp);

    /**
     * @brief Removes a unicast neighbor to the specified interface.
     *
     * Removes a unicast neighbor to a specified interface. Will enable multicast
     * if no unicast neighbors are present.
     *
     * @param neighborIp IP of the static neighbor.
     * @param interfaceType The type of interface this neighbor is being added to.
     * @param id The interface ID of the interface that this neighbor is being added to.
     */
    void deleteNeighbor(const IPAddress& neighborIp);

    /**
     * @brief Retrieves information about a specific neighbor.
     *
     * Searches for and returns the NeighborInfo structure associated with the given
     * neighbor IP address, allowing for inspection or modification of the neighbor's state.
     *
     * @param neighborIp IP address of the neighbor.
     * @return Optional reference to the neighbor's information if found.
     */
    EigrpConfigs::NeighborInfo* lookup(const IPAddress& neighborIp);

    /**
     * @brief Handles the removal of a neighbor by cleaning up associated routes and timers.
     *
     * Performs cleanup operations when a neighbor is removed, including removing
     * routes learned from the neighbor, cancelling active timers, and updating the
     * topology table to reflect the neighbor's departure.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param neighborIp Reference to neighbors IP
     */
    void onDown(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

    /**
     * @brief Handles the restart of a neighbor by reinitializing its state.
     *
     * Resets the neighbor's state machine and re-establishes the neighbor relationship
     * after a graceful restart, ensuring continuity in routing operations.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param neighborIp Reference to neighbors ip address.
     */
    void onRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

    /**
     * @brief Initiates a graceful restart of the EIGRP process.
     *
     * Performs a controlled restart of the EIGRP process, maintaining neighbor relationships
     * and minimizing routing disruptions by retaining routing information during the restart.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param neighborIp Reference to neighbors ip address.
     */
    void startGracefulRestart(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);
    
    // Neighbor management
    std::shared_mutex neighborMutex; ///< Shared mutex for neighbor operations.
    std::unordered_map<IPAddress, EigrpConfigs::NeighborInfo*> neighbors; ///< Map of neighbor IPs to their information.
};
}

#endif // NEIGHBOR_TABLE
