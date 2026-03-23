// EigrpInterface.h

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
#include "configs/RegistryReference.hpp"
#include "configs/registry/router/EigrpInterfaceRegistry.h"

namespace interface { class Interface; class InterfaceConfigs; }

class Internal_EigrpTest;

namespace routing::eigrp
{
class Eigrp;

/**
 * @class EigrpInterface
 * @brief Represents an interface participating in the EIGRP process.
 */
class EigrpInterface
{
public:
    friend class Internal_EigrpTest;
    EigrpInterface(Eigrp& eigrpSystem, config::Reference<config::EigrpInterfaceRegistry>& ifaceReg, interface::Interface& interface);
    ~EigrpInterface();

    EigrpInterface(const EigrpInterface&) = delete;
    EigrpInterface& operator=(const EigrpInterface&) = delete;

    EigrpInterface(EigrpInterface&&) = delete;
    EigrpInterface& operator=(EigrpInterface&&) = delete;

    void setPassiveMode(bool passive);
    void setMulticast(bool state);
    const uint8_t* multicastEnabled();

    void notifyRoutingChange(const std::vector<const RouteInfo*>& changedRoutes);

    void startDampening();
    void triggerDampeningOnRouteChange();
    void checkDampeningStatus();
    bool recordDampeningEvent();
    void onDampeningResetExpire();
    void onDampeningRestartExpire();
    void onDampeningIntervalExpire();

    bool isAuthEnabled() const
    {
        return configs->get<config::EigrpInterface::AUTHENTICATION_MODE>().load() != config::eigrp::AuthType::NONE;
    }

    config::Reference<config::EigrpInterfaceRegistry> configs; ///< Registry-backed configuration for this interface.

    // Runtime state (not persisted in registry)
    std::atomic<bool> multicastEnabledFlag{true};
    std::atomic<uint64_t> localMetric{0};
    std::atomic<uint8_t> DSCP{0};
    std::vector<types::IPPrefix> pendingSummaryRoutes;
    bool isPointToPoint{false};

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

    double penalty = 0;
    std::atomic<uint32_t> prefixCount = 0;
    uint8_t restartCounter = 0;
    std::atomic<bool> isSupressed = false;
    std::deque<std::chrono::steady_clock::time_point> routeChangeTimes;

    std::unordered_map<TLVType, std::unordered_set<types::IPAddress>> tlvTypes;

    std::set<types::IPPrefix> connectedRoutes;

    uint32_t interfaceKey;

    types::IPAddress ifaceAddress;

private:
    Eigrp& base;

    interface::Interface* currentInterface; ///< Pointer to the current network interface.
    interface::InterfaceConfigs* currentInterfaceInfo; ///< Pointer to the current interface's IP information.

    ReliableTransport rtp;
    TopologyController topology;
    NeighborTable ntable;
    AuthHandler auth;
    InterfaceMetrics metrics;
    RouteAggregator aggregator;
    InterfaceTimers tmgr;
};
} // namespace routing

#endif // EIGRP_INTERFACE_H
