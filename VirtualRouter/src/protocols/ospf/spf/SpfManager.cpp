// SpfManager.cpp

#include "SpfManager.h"
#include "SpfEngine.h"
#include "OspfRouteManager.h"
#include <TimeManager.h>
#include <OspfArea.h>
#include <OspfProcess.h>

#include <RouterLsaV2.hpp>
#include <NetworkLsaV2.hpp>
#include <RouterLsaV3.hpp>
#include <NetworkLsaV3.hpp>

namespace OSPF
{
SpfManager::SpfManager(OspfArea& area, TimeManager& tmgr)
    : area(area), tmgr(tmgr), rib(area.process().getRib())
{}

template<typename Policy>
void SpfManager::requestSpf()
{
    requested.store(true, std::memory_order_release);

    if (spfRunning.load(std::memory_order_relaxed))
    {
        reschedule.store(true, std::memory_order_release);
        return;
    }

    if (spfScheduled.load(std::memory_order_acquire))
        return;

    uint32_t delay = computeNextDelay();
    scheduleSpf<Policy>(delay);
}

template<typename Policy>
void SpfManager::onSpfTimer()
{
    spfScheduled.store(false, std::memory_order_relaxed);

    // Spf already running
    if (spfRunning.exchange(true))
        return;

    requested.store(false, std::memory_order_release);

    // Run SPF
    runSpf<Policy>();

    lastSpfTime.store(std::chrono::steady_clock::now(), std::memory_order_release);
    spfRunning.store(false, std::memory_order_release);

    if (reschedule.exchange(false))
    {
        uint32_t delay = computeNextDelay();
        scheduleSpf<Policy>(delay);
        return;
    }

    // No pending changes
    currentDelayMs.store(0, std::memory_order_release);
}

template<typename Policy>
void SpfManager::scheduleSpf(uint32_t delayMs)
{
    if (spfScheduled.exchange(true))
        return;

    auto delay = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    timerId = tmgr.addTimer(delay, [this](uint32_t) {
        this->onSpfTimer<Policy>();
    });
}

template <typename Policy>
void SpfManager::runSpf()
{
    // Create graph
    SpfTopology<Policy> topo(area);
    // Run Dijkstra on graph
    SpfResult spfRes = SpfEngine::run<Policy>(topo);

    std::vector<std::pair<IPPrefix, OspfPath>> pathList;

    // Look up networks from Dikjstra results
    RouteManager::deriveIntraAreaRoutes<Policy>(spfRes, pathList, area);
    RouteManager::deriveInterAreaRoutes<Policy>(spfRes, pathList, area);

    auto summaryChanges = rib.replaceArea(area, pathList);

    // Update Ranges
    area.syncRangeRuntime(pathList);

    area.process().table.consumeSpfResult(area.areaId, spfRes);

    if (area.process().isABR())
    {
        // Reoriginate intra as inter 
        if constexpr (std::is_same_v<std::remove_cv_t<typename Policy::NetworkLsa>, NetworkLsaV2>)
            area.process().reoriginateSummaries<SummaryNetworkLsa>(area, summaryChanges);
        else
            area.process().reoriginateSummaries<InterAreaPrefixLsa>(area, summaryChanges);
    }

    spfResult = std::move(spfRes);
}

uint32_t SpfManager::computeNextDelay()
{
    auto& cfgs = area.process().getConfigs();
    uint32_t initDelayMs = cfgs.get<Config::Ospf::SPF_THROTTLE_DELAY>().load();
    uint32_t holdTimeMs = cfgs.get<Config::Ospf::SPF_THROTTLE_HOLD>().load();
    uint32_t maxHoldTimeMs = cfgs.get<Config::Ospf::SPF_THROTTLE_MAX>().load();

    uint32_t prev = currentDelayMs.load(std::memory_order_relaxed);
    uint32_t backoff;

    if (prev == 0)
        backoff = initDelayMs;
    else
        backoff = std::min(prev * 2, maxHoldTimeMs);

    auto now = std::chrono::steady_clock::now();
    auto last = lastSpfTime.load(std::memory_order_relaxed);

    uint32_t holdRemaining = 0;
    if (last != std::chrono::steady_clock::time_point{})
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (elapsed < holdTimeMs)
            holdRemaining = static_cast<uint32_t>(holdTimeMs - elapsed);
    }

    uint32_t next = std::max(backoff, holdRemaining);

    currentDelayMs.store(backoff, std::memory_order_release);
    return next;
}

template void SpfManager::requestSpf<PolicyV2>();
template void SpfManager::requestSpf<PolicyV3>();

template void SpfManager::onSpfTimer<PolicyV2>();
template void SpfManager::onSpfTimer<PolicyV3>();

template void SpfManager::scheduleSpf<PolicyV2>(uint32_t);
template void SpfManager::scheduleSpf<PolicyV3>(uint32_t);

template void SpfManager::runSpf<PolicyV2>();
template void SpfManager::runSpf<PolicyV3>();
}
