// Eigrp.h

#ifndef EIGRP_CORE_H
#define EIGRP_CORE_H

#include <utility>
#include <cstdint>
#include <mutex>
#include <cstring>
#include <shared_mutex>

#include "EigrpConfig.h"
#include "EigrpInterfaceManager.h"
#include "EigrpMetrics.h"
#include "EigrpPacketBuilder.hpp"
#include "EigrpTopology.h"
#include <EigrpTypes.hpp>

class Internal_EigrpTest;
class VirtualRouter;

enum class InterfaceType;
enum class AddressFamily : uint8_t;

namespace Protocol
{
/**
 * @class Eigrp
 * @brief Manages EIGRP protocol operations including initialization, shutdown, network configuration, and route calculations.
 *
 * The Eigrp class is responsible for overseeing the overall EIGRP operations, handling
 * network configurations, managing EIGRP interfaces, processing routing updates, and
 * maintaining the routing table. It supports both Classic and Named EIGRP modes.
 */
class Eigrp
{
public:
    using InterfaceKey = std::pair<InterfaceType, float>;
    friend class ::Internal_EigrpTest;
    /**
     * @brief Constructs an Eigrp instance.
     *
     * Initializes the EIGRP process with the specified Autonomous System number and
     * address family, setting up necessary configurations and preparing for network operations.
     *
     * @param as Autonomous System number.
     * @param af Address family (IPv4/IPv6).
     */
    Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named = false);

    /**
     * @brief Destructor for Eigrp.
     *
     * Shuts down the EIGRP process gracefully, ensuring that all interfaces are properly
     * closed, timers are canceled, and resources are cleaned up to prevent memory leaks.
     */
    virtual ~Eigrp();

    /**
     * @brief Initializes the EIGRP process.
     *
     * Sets up necessary configurations, initializes interfaces, starts Hello timers,
     * and begins the process of establishing neighbor relationships.
     */
    virtual void start();

    /**
     * @brief Shuts down the EIGRP process gracefully.
     *
     * Terminates all EIGRP operations, cancels active timers, removes routes from
     * the routing table, and cleans up any allocated resources to ensure a clean shutdown.
     */
    virtual void shutdown();

    /**
     * @brief Restarts the EIGRP process.
     *
     * Completely restarts the EIGRP process, resetting configurations, clearing routing tables,
     * and re-establishing neighbor relationships from scratch.
     */
    void restart();

    /**
     * @brief Cleans up all EIGRP configurations and state.
     *
     * Removes all EIGRP configurations, clears routing tables, cancels timers,
     * and frees allocated resources to ensure a complete cleanup of the EIGRP process.
     */
    void cleanup();

    /**
     * @brief Performs periodic maintenance tasks.
     *
     * Executes routine maintenance operations such as pruning stale routes,
     * updating neighbor states, and managing timers to ensure the EIGRP process
     * remains healthy and up-to-date with the network state.
     */
    void runMaintenance();

    /**
     * @brief Calculates the Router ID based on interface addresses.
     *
     * Determines the Router ID by selecting the highest IP address among all configured
     * interfaces or using a manually configured static Router ID, ensuring a unique identifier
     * for the EIGRP process.
     */
    void calculateRID();

    /**
     * @brief Tests if an IP address matches any of the configured EIGRP networks.
     *
     * Checks whether the provided IP address falls within any of the networks
     * configured for EIGRP, determining if EIGRP operations should be active
     * on that interface.
     *
     * @param testIp IP address to test.
     * @return True if the IP address matches a configured network, false otherwise.
     */
    bool isInNetworkRange(const uint8_t* testIp);

    /**
     * @brief Retrieves the Virtual Router ID.
     *
     * Provides the virtual Router ID assigned to the EIGRP process, used in
     * routing advertisements and neighbor identification.
     *
     * @return ByteString representing the virtual Router ID.
     */
    inline uint32_t getVirtualRouterID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return virtualRouterID; }

    /**
     * @brief Retrieves the Router ID.
     *
     * Returns the Router ID configured for the EIGRP process, serving as a unique
     * identifier within the EIGRP routing domain.
     * @return ByteString representing the Router ID.
     */
    inline uint8_t* routerID(uint8_t* out) { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return routerID.ID; }
    inline uint32_t routerID() { std::shared_lock<std::shared_mutex> lock(eigrpDataMutex); return readU32(routerID.ID); }

    void setRouterID(const uint8_t* routerId) { std::memcpy(routerID.ID, routerId, 4); routerID.isStatic = true;}
    void clearRouterID() { routerID.isStatic = false; calculateRID(); }

    VirtualRouter* routingInstance; ///< Routing instance coorsponding with the current process.
    bool namedMode = false; ///< Indicates if running named mode.
    const uint32_t asNumber; ///< Autonomous System number.
    const AddressFamily addressFamily; ///< Address family (IPv4/IPv6).
    EigrpConfigs::RouterID routerID; ///< Router ID configuration.
    uint16_t virtualRouterID = 0x0000; ///< Virtual Router ID.

    std::unordered_map<IPAddress, EigrpConfigs::NeighborInfo*> allNeighbors;
    std::mutex neighborMutex;

public:
    EigrpConfig configMgr;
    EigrpInterfaceManager interfaceMgr;
    EigrpMetrics metrics;
    EigrpTopology topology;
};
}

/**
 * @var currentEigrp
 * @brief Global pointer to the current EIGRP autonomous system.
 *
 * Maintains a pointer to the active EIGRP autonomous system, allowing
 * for global access without ownership concerns.
 */
extern Protocol::Eigrp* currentEigrp;

/**
 * @var currentEigrpNamed
 * @brief Global pointer to the current Named EIGRP system.
 *
 * Maintains a pointer to the current active EIGRP named system,
 * allowing for global access without ownership concerns.
 */
extern Protocol::EigrpNamed* currentEigrpNamed;

/**
 * @var currentEigrpInterface
 * @brief Global pointer to the current EIGRP interface
 *
 * Maintains a pointer to the current EIGRP interface being configured,
 * allowing for global access without ownership concerns.
 */
extern Protocol::EigrpInterface* currentEigrpInterface;

#endif // EIGRP_CORE_H
