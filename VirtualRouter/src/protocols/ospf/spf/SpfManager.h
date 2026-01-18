// SpfManager.h

#ifndef SPF_MANAGER_H
#define SPF_MANAGER_H

#include <cstdint>
#include <atomic>
#include <OspfTypes.hpp>

class TimeManager;

namespace OSPF
{
class OspfArea;
class OspfRib;
struct SpfResult;

class SpfManager
{
public:
    explicit SpfManager(OspfArea& area, TimeManager& tmgr);

    template<typename Policy>
    void requestSpf();

    template<typename Policy>
    void onSpfTimer();

private:
    template<typename Policy>
    void scheduleSpf(uint32_t delayMs);
    uint32_t computeNextDelay();

    template <typename Policy>
    void runSpf();

private:
    OspfArea& area;
    TimeManager& tmgr;
    OspfRib& rib;

    std::atomic<bool> requested{false};
    std::atomic<bool> spfScheduled{false};
    std::atomic<bool> spfRunning{false};
    std::atomic<bool> reschedule{false};

    std::atomic<std::chrono::steady_clock::time_point> lastSpfTime;

    std::atomic<OspfConfigs::Throttle>& throttle;

    std::atomic<uint32_t> currentDelayMs{0};
    uint32_t timerId;
};
}

#endif // SPF_MANAGER_H
