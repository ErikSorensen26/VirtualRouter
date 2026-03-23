// SpfManager.h

#ifndef SPF_MANAGER_H
#define SPF_MANAGER_H

#include <ControlScheduler.h>
#include <chrono>
#include <cstdint>
#include <atomic>

#include "SpfTypes.hpp"
#include "SpfEngine.h"

namespace routing::ospf
{
class Area;
class OspfRib;

class SpfManager
{
public:
    explicit SpfManager(Area& area);

    template<typename Policy>
    void requestSpf();

    template<typename Policy>
    void onSpfTimer();

    SpfResult spfResult;

private:
    template<typename Policy>
    void scheduleSpf(uint32_t delayMs);
    uint32_t computeNextDelay();

    template <typename Policy>
    void runSpf();

private:
    SpfEngine engine;

    Area& area;
    OspfRib& rib;

    std::atomic<bool> requested{false};
    std::atomic<bool> spfScheduled{false};
    std::atomic<bool> spfRunning{false};
    std::atomic<bool> reschedule{false};

    std::atomic<std::chrono::steady_clock::time_point> lastSpfTime;

    std::atomic<uint32_t> currentDelayMs{0};
    uint32_t timerId;
};
} // namespace routing

#endif // SPF_MANAGER_H

