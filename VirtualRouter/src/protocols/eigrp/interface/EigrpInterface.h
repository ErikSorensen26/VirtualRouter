// EigrpInterface.h

#ifndef EIGRP_INTERFACE_H
#define EIGRP_INTERFACE_H

#include <IPAddress.hpp>
#include "AuthHandler.h"
#include "InterfaceMetrics.h"
#include "ReliableTransport.h"
#include "RouteAggregator.h"
#include "NeighborTable.h"
#include "InterfaceTimers.h"
#include "TopologyController.h"
#include "AuthHandler.h"
#include <unordered_map>
#include <deque>

class Internal_EigrpTest;
class Interface;
class InterfaceConfigs;
namespace EigrpConfigs
{
struct InterfaceConfigs;
}

namespace Eigrp
{
class Eigrp;

/**
 * @class EigrpInterface
 * @brief Represents an interface participating in the EIGRP process.
 *
 * The EigrpInterface class manages EIGRP operations specific to a network interface,
 * including sending and receiving EIGRP packets, maintaining neighbor relationships,
 * handling routing updates, and managing timers and retransmissions.
 */
class EigrpInterface
{
public:
    friend class ::Internal_EigrpTest;
    EigrpInterface(Eigrp& eigrpSystem, EigrpConfigs::InterfaceConfigs& intConfigs, Interface& interface);
    ~EigrpInterface();

    EigrpInterface(const EigrpInterface&) = delete;
    EigrpInterface& operator=(const EigrpInterface&) = delete;

    EigrpInterface(EigrpInterface&&) = delete;
    EigrpInterface& operator=(EigrpInterface&&) = delete;

    void setPassiveMode(bool passive);
    void setMulticast(bool state);
    const uint8_t* multicastEnabled();

    void notifyRoutingChange(const std::vector<const RouteInfo*>& changedRoutes);

    // Get the ip address of the interaface
    void startDampening();
    void triggerDampeningOnRouteChange();
    void checkDampeningStatus();
    bool recordDampeningEvent();
    void onDampeningResetExpire();
    void onDampeningRestartExpire();
    void onDampeningIntervalExpire();

    EigrpConfigs::InterfaceConfigs& configs; ///< Configuration settings for the interface.

    ReliableTransport& getRtp() { return rtp; }
    InterfaceMetrics& getMetrics() { return metrics; }
    InterfaceTimers& getTimers() { return tmgr; }
    TopologyController& getTopController() { return topology; }
    NeighborTable& getNTable() { return ntable; }
    RouteAggregator& getAggregator() { return aggregator; }
    Eigrp& getBase() const { return base; }
    AuthHandler& getAuth() { return auth; }
    Interface* getIface() const { return currentInterface; }
    InterfaceConfigs& getIfaceCfg() { return *currentInterfaceInfo; }

    double penalty = 0;
    std::atomic<uint32_t> prefixCount = 0;
    uint8_t restartCounter = 0;
    std::atomic<bool> isSupressed = false;
    std::deque<std::chrono::steady_clock::time_point> routeChangeTimes;

    std::unordered_map<TLVType, std::unordered_set<IPAddress>> tlvTypes;

    std::set<IPPrefix> connectedRoutes;

    uint32_t interfaceKey;
    
    IPAddress ifaceAddress;

private:
    Eigrp& base;

    Interface* currentInterface; ///< Pointer to the current network interface.
    InterfaceConfigs* currentInterfaceInfo; ///< Pointer to the current interface's IP information.

    ReliableTransport rtp;
    TopologyController topology;
    NeighborTable ntable;
    AuthHandler auth;
    InterfaceMetrics metrics;
    RouteAggregator aggregator;
    InterfaceTimers tmgr;
};
}

#endif // EIGRP_INTERFACE_H
