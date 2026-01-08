// SpfManager.cpp

#include "SpfManager.h"
#include "SpfEngine.h"
#include <TimeManager.h>
#include <OspfArea.h>
#include <OspfTopology.h>
#include <OspfProcess.h>
#include <OspfTypes.hpp>

#include <RouterLsaV2.hpp>
#include <NetworkLsaV2.hpp>
#include <RouterLsaV3.hpp>
#include <NetworkLsaV3.hpp>

namespace OSPF
{
SpfManager::SpfManager(OspfArea& area, TimeManager& tmgr)
    : area(area), tmgr(tmgr), isV3(area.topology().process.isV3), throttle(area.topology().process.getConfigs().spfThrottle)
{}

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
    scheduleSpf(delay);
}

void SpfManager::onSpfTimer()
{
    spfScheduled.store(false, std::memory_order_relaxed);

    // Spf already running
    if (spfRunning.exchange(true))
        return;

    runSpf();
}

void SpfManager::scheduleSpf(uint32_t delayMs)
{
    if (spfScheduled.exchange(true))
        return;

    auto delay = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    timerId = tmgr.addTimer(delay, [this]() {
        this->onSpfTimer();
    });
}

void SpfManager::runSpf()
{
    requested.store(false, std::memory_order_release);

    // Run spf
    auto result = isV3
        ? SpfEngine::run<RouterLsaV3, NetworkLsaV2>(area)
        : SpfEngine::run<RouterLsaV2, NetworkLsaV2>(area);
    // run intra area route injection

    lastSpfTime.store(std::chrono::steady_clock::now(), std::memory_order_release);
    spfRunning.store(false, std::memory_order_release);

    if (reschedule.exchange(false))
    {
        uint32_t delay = computeNextDelay();
        scheduleSpf(delay);
        return;
    }

    // No pending changes
    currentDelayMs.store(0, std::memory_order_release);
}

uint32_t SpfManager::computeNextDelay()
{
    auto thr = throttle.load(std::memory_order_acquire);

    uint32_t prev = currentDelayMs.load(std::memory_order_relaxed);
    uint32_t backoff;

    if (prev == 0)
        backoff = thr.initDelayMs;
    else
        backoff = std::min(prev * 2, thr.maxHoldTimeMs);

    auto now = std::chrono::steady_clock::now();
    auto last = lastSpfTime.load(std::memory_order_relaxed);

    uint32_t holdRemaining = 0;
    if (last != std::chrono::steady_clock::time_point{})
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (elapsed < thr.holdTimeMs)
            holdRemaining = static_cast<uint32_t>(thr.holdTimeMs - elapsed);
    }

    uint32_t next = std::max(backoff, holdRemaining);

    currentDelayMs.store(backoff, std::memory_order_release);
    return next;
}
}
