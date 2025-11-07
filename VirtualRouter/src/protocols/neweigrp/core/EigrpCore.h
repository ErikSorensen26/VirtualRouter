// Eigrp.h

#ifndef EIGRP_CORE_H
#define EIGRP_CORE_H

#include <utility>
#include <cstdint>
#include <mutex>
#include <cstring>
#include <shared_mutex>

#include "EigrpConfig.h"
#include "InterfaceManager.h"
#include "MetricCalculator.h"
#include "EigrpTopology.h"
#include "RouteManager.h"
#include <EigrpTypes.hpp>

class Internal_EigrpTest;
class VirtualRouter;

enum class InterfaceType : uint8_t;
enum class AddressFamily : uint8_t;

namespace Eigrp
{
struct EigrpAutonomousSystem
{
    Eigrp* ipv4 = nullptr;
    Eigrp* ipv6 = nullptr;
    bool ipv4Named = false;
    bool ipv6Named = false;
};

struct EigrpNamed
{
    Eigrp* ipv4 = nullptr;
    Eigrp* ipv6 = nullptr;
};

struct EigrpInterfaceInstance
{
    EigrpInterface* IPv4; ///< Pointer to the IPv4 EIGRP interface.
    EigrpInterface* IPv6; ///< Pointer to the IPv6 EIGRP interface.
};

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
    Eigrp(uint32_t as, AddressFamily af, VirtualRouter* vrf, bool named = false);
    virtual ~Eigrp();
    virtual void start();
    virtual void shutdown();
    void restart();
    void runMaintenance();
    void calculateRID();
    bool isInNetworkRange(const uint8_t* testIp);

    void addGlobalNeighbor(const IPAddress& neighborIp, Neighbor* neighbor);
    void delGlobalNeighbor(const IPAddress& neighborIp);
    size_t totalNeighbors() { return allNeighbors.size(); }
    void broadcastRouteChanges(const std::vector<const RouteInfo*>& changedRoutes);

    void refreshInterfaceList();

    EigrpConfig& getGlobalConfigMgr() { return configMgr; }
    InterfaceManager& getIfaceMgr() { return ifaceMgr; }
    MetricCalculator& getMetricCalc() { return metricCalculator; }
    GlobalAggregator& getAggregator() { return aggregator; }
    EigrpTopology& getTopology() { return topology; }

public:
    struct RouterID
    {
        uint8_t ID[4] = { 0x00, 0x00, 0x00, 0x00 }; ///< Router ID.
        bool isStatic = false;                 ///< Indicates if the Router ID is static.
    };

    inline uint16_t getVirtualRouterID() const { return virtualRouterID; }
    inline uint8_t* routerID(uint8_t* out) const { std::memcpy(out, rid.ID, 4); return out; }
    inline uint32_t routerID() const { return readU32(rid.ID); }

    bool isNamed() const { return namedMode; }
    uint32_t getAS() const { return asNumber; }
    AddressFamily getAF() const { return addressFamily; }

    void setRouterID(const uint8_t* routerId) { std::memcpy(rid.ID, routerId, 4); rid.isStatic = true;}
    void clearRouterID() { rid.isStatic = false; calculateRID(); }

    VirtualRouter* routingInstance; ///< Routing instance coorsponding with the current process.

    RouteManager routeManager;
    std::unordered_map<IPAddress, Neighbor*> allNeighbors;
    std::mutex neighborMutex;

private:

    EigrpConfig configMgr;
    InterfaceManager ifaceMgr;
    MetricCalculator metricCalculator;
    GlobalAggregator aggregator;
    EigrpTopology topology;

    bool namedMode = false; ///< Indicates if running named mode.
    const uint32_t asNumber; ///< Autonomous System number.
    const AddressFamily addressFamily; ///< Address family (IPv4/IPv6).
    RouterID rid; ///< Router ID configuration.
    uint16_t virtualRouterID = 0x0000; ///< Virtual Router ID.
};

class ClassicEigrp : public Eigrp
{
public:
    ClassicEigrp(uint32_t& as, AddressFamily af, VirtualRouter* vrf) : Eigrp(as, af, vrf) {}
    void initializeEigrp();
    void shutdown();
};

class NamedEigrp : public Eigrp
{
private:
    std::string processName; ///< Name of the Named EIGRP process.

public:
    NamedEigrp(uint32_t& as, AddressFamily af, const std::string& name, VirtualRouter* vrf, bool multicast);
    void initializeEigrp();
    void shutdown();
    void configureInterface(uint32_t interfaceId, const EigrpConfigs::InterfaceConfigs& configs);
};
}

#endif // EIGRP_CORE_H
