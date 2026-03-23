// FloodManager.h

#ifndef FLOOD_MANAGER_H
#define FLOOD_MANAGER_H

#include <cstdint>
#include <atomic>
#include "FloodQueue.hpp"

namespace core { class ProcessQueue; }

namespace routing::ospf
{
class Area;
class OspfRib;

class FloodManager
{
public:
    explicit FloodManager(Area& area);

    void enqueueFlood(LsaRecordRef& record, const FloodInfo& info);
    void enqueueFlood(LsaRecordRef&& record, const FloodInfo& info);

private:
    void startFloodTimer();

    void onFloodTimer();

    void runFlood();

private:
    Area& area;
    FloodQueue fq;

    std::atomic<bool> timerActive{0};
    uint32_t timerId;
};
} // namespace routing

#endif // FLOOD_MANAGER_H

