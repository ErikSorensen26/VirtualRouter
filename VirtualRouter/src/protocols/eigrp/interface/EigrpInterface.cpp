// EigrpInterface.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "EigrpInterface.h"
#include "eigrp/core/Eigrp.h"

namespace EIGRP
{
EigrpInterface::EigrpInterface(Eigrp& eigrpSystem, Config::EigrpInterfaceRegistry& ifaceReg, Interface& interface)
  : configs(ifaceReg),
    interfaceKey(interface.configs.key),
    base(eigrpSystem),
    currentInterface(&interface),
    currentInterfaceInfo(&interface.configs),
    rtp(*this),
    topology(ntable, eigrpSystem.getTopology().duel, *this),
    ntable(*this),
    auth(ifaceReg, eigrpSystem.routingInstance->getGlobal().keyChainManager),
    metrics(*this),
    aggregator(*this),
    tmgr(*this, eigrpSystem.getScheduler())
{
    // Set local ip
    if (base.getAF() == AddressFamily::IPv4)
    {
        ifaceAddress.setV4(interface.configs.ipv4.getPrimaryAddress().addr);
    }
    else
    {
        ifaceAddress.setV6(interface.configs.ipv6.getLocalAddress().addr);
    }

    // Add pending summary routes if needed
    for (const auto& prefix : pendingSummaryRoutes)
        aggregator.installSummary(prefix);
    pendingSummaryRoutes.clear();

    // Gather locked values for local metric calculation
    uint32_t delay      = currentInterfaceInfo->delay.load(std::memory_order_relaxed);
    uint32_t bandwidth  = currentInterfaceInfo->bandwidth.load(std::memory_order_relaxed);
    uint8_t reliability = currentInterfaceInfo->reliability.load(std::memory_order_relaxed);
    uint8_t load        = currentInterfaceInfo->load.load(std::memory_order_relaxed);

    // Check if this interface is passive
    if (base.getGlobalConfigMgr().isPassive(interfaceKey))
        setPassiveMode(true);

    localMetric = metrics.calculateCompositeMetric(
        load, reliability, delay * 1'000'000, bandwidth);

    // Handle unicast neighbors
    std::unordered_set<IPAddress> unicastNeighbors = base.getGlobalConfigMgr().getUnicastNeighbors(interfaceKey);
    if (!unicastNeighbors.empty())
    {
        multicastEnabledFlag.store(false, std::memory_order_release);
        for (auto neighbor : unicastNeighbors)
            ntable.createNeighbor(neighbor, Neighbor::Version::UNKNOWN, true);
    }

    startDampening();
    tmgr.startHello();
}

EigrpInterface::~EigrpInterface()
{
    // Remove interface from other tables
    uint32_t id = base.getAS();
    AddressFamily af = base.getAF();

    // Remove routes
    base.getTopology().clearConnected(*this);

    if (currentInterface->eigrpInterfaceList.find(id) != currentInterface->eigrpInterfaceList.end())
    {
        if (af == AddressFamily::IPv4)
            currentInterface->eigrpInterfaceList[id].IPv4 = nullptr;
        else if (af == AddressFamily::IPv6)
            currentInterface->eigrpInterfaceList[id].IPv6 = nullptr;
        if (!currentInterface->eigrpInterfaceList[id].IPv4 && !currentInterface->eigrpInterfaceList[id].IPv6)
            currentInterface->eigrpInterfaceList.erase(id);
    }
}

void EigrpInterface::notifyRoutingChange(const std::vector<const RouteInfo*>& changedRoutes)
{
    if (changedRoutes.empty() || isSupressed.load(std::memory_order_relaxed))
        return;

    std::vector<Neighbor*> unicastNeighbors = ntable.lookupUnicast();
    bool hasMulticast = unicastNeighbors.size() < ntable.size();

    for (auto* neighbor : unicastNeighbors)
    {
        rtp.sendUpdate(neighbor, changedRoutes);
    }

    if (hasMulticast && multicastEnabledFlag.load(std::memory_order_relaxed))
    {
        rtp.sendUpdate(nullptr, changedRoutes);
    }
}

void EigrpInterface::setPassiveMode(bool passive)
{
    configs.get<Config::EigrpInterface::PASSIVE_INTERFACE>().set(passive);
    if (passive)
    {
        for (auto it = ntable.neighbors.begin(); it != ntable.neighbors.end();)
        {
            tmgr.cancelHoldTimer(it->second);
            auto next = std::next(it);
            Neighbor& nbr = it->second;
            ntable.onDown(nbr);
            it = next;
        }
        tmgr.stopHello();
    }
    else
    {
        tmgr.startHello();
    }
}

void EigrpInterface::setMulticast(bool state)
{
    if (state && !multicastEnabledFlag.load(std::memory_order_relaxed))
    {
        multicastEnabledFlag.store(true, std::memory_order_release);
    }
    else if (multicastEnabledFlag.load(std::memory_order_relaxed))
    {
        multicastEnabledFlag.store(false, std::memory_order_release);
        ntable.removeAllMulticast();
    }
}

const uint8_t* EigrpInterface::multicastEnabled()
{
    return multicastEnabledFlag.load(std::memory_order_relaxed)
        ? (base.getAF() == AddressFamily::IPv4)
            ? EIGRP_MULTICAST_ADDRESS
            : EIGRP_MULTICAST_ADDRESS_V6
        : nullptr;
}

void EigrpInterface::startDampening()
{
    if (base.getGlobalConfigMgr().getDampening())
        tmgr.startDampeningIntervalTimer();
}

bool EigrpInterface::recordDampeningEvent()
{
    auto& cfg = base.getGlobalConfigMgr();
    if (!cfg.getDampening()) return true;

    auto now = std::chrono::steady_clock::now();
    routeChangeTimes.push_back(now);

    // Drop old changes outside of interval
    const auto intervalSec = std::chrono::seconds(configs.get<Config::EigrpInterface::DAMPENING_INTERVAL_TIME>().load());
    while (!routeChangeTimes.empty() && now - routeChangeTimes.front() > intervalSec)
        routeChangeTimes.pop_front();

    return !isSupressed.load(std::memory_order_relaxed);
}

void EigrpInterface::triggerDampeningOnRouteChange()
{
    auto& cfg = base.getGlobalConfigMgr();
    uint32_t maxPrefix = cfg.getMaximumPrefixes();
    if (maxPrefix == 0) return;

    prefixCount.fetch_add(1, std::memory_order_relaxed);

    double changePercent = (static_cast<double>(routeChangeTimes.size()) / maxPrefix) * 100.0;
    double triggerPercent = configs.get<Config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().load();

    if (changePercent >= triggerPercent && !isSupressed.load(std::memory_order_relaxed))
    {
        isSupressed.store(true, std::memory_order_release);
        restartCounter++;

        tmgr.restartDampeningResetTimer();
    }
}

void EigrpInterface::checkDampeningStatus()
{
    if (!isSupressed.load(std::memory_order_relaxed)) return;

    auto& cfg = base.getGlobalConfigMgr();
    bool reachedLimit = restartCounter >= cfg.getDampeningRestartCount();

    isSupressed.store(false, std::memory_order_release);
    routeChangeTimes.clear();
    prefixCount.store(0, std::memory_order_release);

    if (!reachedLimit)
        tmgr.restartDampeningRestartTimer();
}

void EigrpInterface::onDampeningResetExpire()
{
    checkDampeningStatus();
}

void EigrpInterface::onDampeningRestartExpire()
{
    restartCounter = 0;
    prefixCount.store(0, std::memory_order_release);
    routeChangeTimes.clear();
}

void EigrpInterface::onDampeningIntervalExpire()
{
    auto& cfg = base.getGlobalConfigMgr();
    if (!cfg.getDampening()) return;

    tmgr.startDampeningIntervalTimer();

    const uint32_t maxPrefixes = cfg.getMaximumPrefixes();
    if (maxPrefixes == 0) return;

    double changePercent = (static_cast<double>(routeChangeTimes.size()) / maxPrefixes) * 100.0;
    double triggerPercent = configs.get<Config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().load();

    if (changePercent >= triggerPercent && !isSupressed.load(std::memory_order_relaxed))
    {
        isSupressed.store(true, std::memory_order_release);
        ++restartCounter;

        tmgr.restartDampeningResetTimer();
    }
}
}
