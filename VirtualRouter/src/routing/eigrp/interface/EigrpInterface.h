/**
 * @file EigrpInterface.h
 * @brief Per-interface EIGRP state and packet handling.
 */

/**
 * @defgroup EIGRP_INTERFACE EIGRP Interface
 * @ingroup EIGRP
 * @brief Per-interface EIGRP state: metrics, timers, auth, route aggregation, topology control.
 */

#ifndef EIGRP_INTERFACE_H
#define EIGRP_INTERFACE_H

#include <IPAddress.h>
#include <unordered_map>
#include <deque>
#include <atomic>
#include <vector>

#include "AuthHandler.h"
#include "InterfaceMetrics.h"
#include "InterfaceTimers.h"
#include "RouteAggregator.h"
#include "TopologyController.h"
#include "eigrp/rtp/ReliableTransport.h"
#include "eigrp/rtp/NeighborTable.h"
#include "configs/registry/router/EigrpInterfaceRegistry.h"
#include "interface/configs/InterfaceType.hpp"
#include "configs/FieldAccessor.hpp"

namespace interface { class Interface; class InterfaceConfigs; }

class Internal_EigrpTest;

namespace routing::eigrp
{
class Eigrp;

/**
 * @brief Encapsulates all per-interface EIGRP state for a single network
 *        interface.
 *
 * @c EigrpInterface is the central object for one EIGRP-enabled interface.
 * It owns the @c ReliableTransport (RTP), @c NeighborTable, @c InterfaceTimers,
 * @c TopologyController, @c RouteAggregator, @c InterfaceMetrics, and
 * @c AuthHandler subsystems.  It is created by @c InterfaceManager when the
 * interface is brought up under the EIGRP process and destroyed when it is
 * removed.
 *
 * Dampening state (penalty, suppression flag, restart counter) is also kept
 * here to rate-limit flapping interfaces.
 *
 * @ingroup EIGRP_INTERFACE
 */
class EigrpInterface
{
public:
    friend class ::Internal_EigrpTest;

    /**
     * @brief Constructs an EigrpInterface and initialises all subsystems.
     * @param eigrpSystem The owning EIGRP process.
     * @param ifaceReg    Registry reference holding the per-interface config.
     * @param interface   The underlying network interface object.
     */
    EigrpInterface(Eigrp& eigrpSystem, config::EigrpInterfaceRegistry& ifaceReg, interface::Interface& interface);

    /**
     * @brief Tears down timers and removes all neighbors before destruction.
     */
    ~EigrpInterface();

    EigrpInterface(const EigrpInterface&) = delete;
    EigrpInterface& operator=(const EigrpInterface&) = delete;

    EigrpInterface(EigrpInterface&&) = delete;
    EigrpInterface& operator=(EigrpInterface&&) = delete;

    /**
     * @brief Puts the interface into or out of passive mode.
     *
     * In passive mode the interface stops sending Hellos and will not form
     * new neighbors, but its connected prefix is still advertised.
     * @param passive @c true to enable passive mode.
     */
    void setPassiveMode(bool passive);

    /**
     * @brief Enables or disables EIGRP multicast transmission on this interface.
     * @param state @c true to enable multicast.
     */
    void setMulticast(bool state);

    /**
     * @brief Returns a pointer to the raw multicast-enabled flag byte, used
     *        by the packet layer when building EIGRP multicast packets.
     * @return Pointer to the internal multicast flag byte.
     */
    const uint8_t* multicastEnabled();

    /**
     * @brief Notifies the interface that one or more routes have changed and
     *        schedules incremental Update packets to neighbors.
     * @param changedRoutes Routes whose metrics or reachability have changed.
     */
    void notifyRoutingChange(const std::vector<const RouteInfo*>& changedRoutes);

    // DAMPENING

    /**
     * @brief Activates route dampening on this interface after the first
     *        suppression threshold is crossed.
     */
    void startDampening();

    /**
     * @brief Increments the dampening penalty when a routing change is
     *        detected and evaluates suppression.
     */
    void triggerDampeningOnRouteChange();

    /**
     * @brief Checks whether the interface is currently suppressed and
     *        updates @c isSupressed accordingly.
     */
    void checkDampeningStatus();

    /**
     * @brief Records a dampening event and returns whether the interface
     *        should be suppressed as a result.
     * @return @c true if the interface crosses the suppress threshold.
     */
    bool recordDampeningEvent();

    /**
     * @brief Called when the dampening reset timer fires; decays the penalty.
     */
    void onDampeningResetExpire();

    /**
     * @brief Called when the dampening restart timer fires; re-enables the
     *        interface after a suppression period.
     */
    void onDampeningRestartExpire();

    /**
     * @brief Called at each dampening interval tick to update internal
     *        rate counters.
     */
    void onDampeningIntervalExpire();

    /**
     * @brief Returns @c true when packet-level authentication is active on
     *        this interface.
     */
    bool isAuthEnabled() const
    {
        return configs.get<config::EigrpInterface::AUTHENTICATION_MODE>().load() != config::eigrp::AuthType::NONE;
    }

    config::EigrpInterfaceRegistry& configs; ///< Registry-backed configuration for this interface.

    // RUNTIME STATE (not persisted in registry)
    std::atomic<bool> multicastEnabledFlag{true};  ///< Whether multicast is currently enabled.
    std::atomic<uint64_t> localMetric{0};           ///< Composite metric contribution from this interface.
    std::atomic<uint8_t> DSCP{0};                   ///< DSCP marking applied to outgoing EIGRP packets.
    std::vector<types::IPPrefix> pendingSummaryRoutes; ///< Summary routes waiting to be installed.
    bool isPointToPoint{false};                     ///< True when the interface is a point-to-point link.

    ReliableTransport& getRtp() { return rtp; }
    InterfaceMetrics& getMetrics() { return metrics; }
    InterfaceTimers& getTimers() { return tmgr; }
    TopologyController& getTopController() { return topology; }
    NeighborTable& getNTable() { return ntable; }
    RouteAggregator& getAggregator() { return aggregator; }
    Eigrp& getBase() const { return base; }
    AuthHandler& getAuth() { return auth; }
    interface::Interface* getIface() const { return currentInterface; }
    interface::InterfaceConfigs& getIfaceCfg() { return *currentInterfaceInfo; }

    double penalty = 0;                              ///< Current dampening penalty value.
    std::atomic<uint32_t> prefixCount = 0;           ///< Number of prefixes currently tracked on this interface.
    uint8_t restartCounter = 0;                      ///< Number of dampening restart cycles completed.
    std::atomic<bool> isSupressed = false;           ///< True while this interface is dampening-suppressed.
    std::deque<std::chrono::steady_clock::time_point> routeChangeTimes; ///< Timestamps of recent route change events.

    std::unordered_map<TLVType, std::unordered_set<types::IPAddress>> tlvTypes; ///< TLV types supported per neighbor address.

    std::set<types::IPPrefix> connectedRoutes; ///< Directly connected prefixes on this interface.

    interface::InterfaceKey interfaceKey; ///< Unique key identifying this interface within the EIGRP process.

    types::IPAddress ifaceAddress; ///< Primary IP address of this interface.

private:
    Eigrp& base;

    interface::Interface* currentInterface; ///< Pointer to the current network interface.
    interface::InterfaceConfigs* currentInterfaceInfo; ///< Pointer to the current interface's IP information.

    ReliableTransport rtp;       ///< Reliable Transport Protocol engine for this interface.
    TopologyController topology; ///< Processes received routes and drives DUAL on this interface.
    NeighborTable ntable;        ///< Per-interface EIGRP neighbor table.
    AuthHandler auth;            ///< Authentication TLV handler.
    InterfaceMetrics metrics;    ///< Composite metric computation for this interface.
    RouteAggregator aggregator;  ///< Per-interface summary route manager.
    InterfaceTimers tmgr;        ///< Hello and hold timer management.
};
} // namespace routing

#endif // EIGRP_INTERFACE_H
