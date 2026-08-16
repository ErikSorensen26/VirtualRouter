// EigrpInterface.cpp

#include <Global.h>
#include <VirtualRouter.h>

#include "hardware/HardwareManager.h"
#include "EigrpInterface.h"
#include "eigrp/core/Eigrp.h"

namespace routing::eigrp
{
EigrpInterface::EigrpInterface(Eigrp& eigrpSystem, config::EigrpInterfaceRegistry& ifaceReg, interface::Interface& interface)
  : interfaceKey(interface.configs.key),
    configs(ifaceReg),
    process(eigrpSystem),
    currentInterface(&interface),
    currentInterfaceInfo(&interface.configs),
    rtp(*this),
    topology(ntable, eigrpSystem.getTopology().dual, *this),
    ntable(*this),
    auth(ifaceReg, eigrpSystem.routingInstance->getGlobal().keyChainManager),
    metrics(*this),
    aggregator(*this),
    tmgr(*this, eigrpSystem.getScheduler())
{
    configs.context().set(this);

    // Set local ip
    if (process.addressFamily == types::AddressFamily::IPv4)
    {
        ifaceAddress.setV4(interface.configs.ipv4.getPrimaryAddress().addr);
        process.getTopology().synchronizeConnected(*this);
    }
    else
    {
        ifaceAddress.setV6(interface.configs.ipv6.getLocalAddress().addr);
        process.getTopology().synchronizeConnected(*this);
    }

    // Add pending summary routes if needed
    for (const auto& prefix : pendingSummaryRoutes)
        aggregator.installSummary(prefix);
    pendingSummaryRoutes.clear();

    // Gather locked values for local metric calculation
    uint32_t delay      = 0;
    uint32_t bandwidth  = static_cast<uint32_t>(interface.configs.hwInfo.bandwidth / 1000);
    uint8_t reliability = 255;
    uint8_t load        = 1;

    // Check if this interface is passive
    setPassive(process.isPassive(interfaceKey));

    localMetric = metrics.calculateCompositeMetric(
        load, reliability, delay * 1'000'000, bandwidth);

    std::set<types::IPPrefix> summaries;
    configs.get<config::EigrpInterface::SUMMARY_ADDRESS>().readEach(
        [&](const config::EigrpSummaryAddress& sum) { summaries.emplace(sum.prefix()); });
    aggregator.installSummaries(summaries);

    // Handle unicast neighbors
    std::unordered_set<types::IPAddress> unicastNeighbors = process.getUnicastNeighbors(interfaceKey);
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
    process.getTopology().clearConnected(*this);
}

void EigrpInterface::addGlobalNeighbor(const types::IPAddress& neighborIp, Neighbor* neighbor)
{
    process.addGlobalNeighbor(neighborIp, neighbor);
}

void EigrpInterface::delGlobalNeighbor(const types::IPAddress& neighborIp)
{
    process.delGlobalNeighbor(neighborIp);
}

const config::EigrpRegistry& EigrpInterface::getConfigs() const
{
    return process.getConfigs();
}

uint32_t EigrpInterface::routerID() const
{
    return process.routerID();
}

uint16_t EigrpInterface::getVirtualRouterID() const
{
    return process.getVirtualRouterID();
}

void EigrpInterface::enqueueSetPassive(bool passive)
{
    process.getScheduler().post([this, passive]() {
        setPassive(passive);
    });
}

void EigrpInterface::enqueueSetShutdown(bool shut)
{
    process.getScheduler().post([this, shut]() {
        setShutdown(shut);
    });
}

void EigrpInterface::enqueueSetSummary(types::IPPrefix prefix, std::optional<std::string> leakMap, bool add)
{
    process.getScheduler().post([this, prefix, leakMap, add]() {
        if (add)
            aggregator.installSummary(prefix);
        else
            aggregator.withdrawSummary(prefix);
    });
}

void EigrpInterface::notifyRoutingChange(const std::vector<const RouteInfo*>& changedRoutes)
{
    if (changedRoutes.empty() || priv.isSupressed.load(std::memory_order_relaxed))
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

void EigrpInterface::setPassive(bool passive)
{
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

void EigrpInterface::setShutdown(bool shut)
{
    if (shut)
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
        process.getTopology().clearConnected(*this);
    }
    else
    {
        process.getTopology().synchronizeConnected(*this);
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
        ? (process.addressFamily == types::AddressFamily::IPv4)
            ? EIGRP_MULTICAST_ADDRESS
            : EIGRP_MULTICAST_ADDRESS_V6
        : nullptr;
}

void EigrpInterface::startDampening()
{
    if (process.getConfigs().get<config::Eigrp::MAXIMUM_PREFIX_DAMPENING>().load())
        tmgr.startDampeningIntervalTimer();
}

bool EigrpInterface::recordDampeningEvent()
{
    if (!process.getConfigs().get<config::Eigrp::MAXIMUM_PREFIX_DAMPENING>().load()) return true;

    auto now = std::chrono::steady_clock::now();
    priv.routeChangeTimes.push_back(now);

    // Drop old changes outside of interval
    const auto intervalSec = std::chrono::seconds(configs.get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>().load());
    while (!priv.routeChangeTimes.empty() && now - priv.routeChangeTimes.front() > intervalSec)
        priv.routeChangeTimes.pop_front();

    return !priv.isSupressed.load(std::memory_order_relaxed);
}

void EigrpInterface::triggerDampeningOnRouteChange()
{
    uint32_t maxPrefix = process.getConfigs().get<config::Eigrp::MAXIMUM_PREFIX>().load();
    if (maxPrefix == 0) return;

    priv.prefixCount.fetch_add(1, std::memory_order_relaxed);

    double changePercent = (static_cast<double>(priv.routeChangeTimes.size()) / maxPrefix) * 100.0;
    double triggerPercent = configs.get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().load();

    if (changePercent >= triggerPercent && !priv.isSupressed.load(std::memory_order_relaxed))
    {
        priv.isSupressed.store(true, std::memory_order_release);
        priv.restartCounter++;

        tmgr.restartDampeningResetTimer();
    }
}

void EigrpInterface::checkDampeningStatus()
{
    if (!priv.isSupressed.load(std::memory_order_relaxed)) return;

    bool reachedLimit = priv.restartCounter >= process.getConfigs().get<config::Eigrp::MAXIMUM_PREFIX_RESTART_COUNT>().load();

    priv.isSupressed.store(false, std::memory_order_release);
    priv.routeChangeTimes.clear();
    priv.prefixCount.store(0, std::memory_order_release);

    if (!reachedLimit)
        tmgr.restartDampeningRestartTimer();
}

void EigrpInterface::onDampeningResetExpire()
{
    checkDampeningStatus();
}

void EigrpInterface::onDampeningRestartExpire()
{
    priv.restartCounter = 0;
    priv.prefixCount.store(0, std::memory_order_release);
    priv.routeChangeTimes.clear();
}

void EigrpInterface::onDampeningIntervalExpire()
{
    if (!process.getConfigs().get<config::Eigrp::MAXIMUM_PREFIX_DAMPENING>().load()) return;

    tmgr.startDampeningIntervalTimer();

    const uint32_t maxPrefixes = process.getConfigs().get<config::Eigrp::MAXIMUM_PREFIX>().load();
    if (maxPrefixes == 0) return;

    double changePercent = (static_cast<double>(priv.routeChangeTimes.size()) / maxPrefixes) * 100.0;
    double triggerPercent = configs.get<config::EigrpInterface::DAMPENING_CHANGE_PERCENT>().load();

    if (changePercent >= triggerPercent && !priv.isSupressed.load(std::memory_order_relaxed))
    {
        priv.isSupressed.store(true, std::memory_order_release);
        ++priv.restartCounter;

        tmgr.restartDampeningResetTimer();
    }
}
} // namespace routing
