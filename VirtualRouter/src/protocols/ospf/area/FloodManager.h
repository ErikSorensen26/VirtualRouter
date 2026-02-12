// FloodManager.h

#ifndef FLOOD_MANAGER_H
#define FLOOD_MANAGER_H

#include <cstdint>
#include <atomic>
#include "FloodQueue.hpp"

class ProcessQueue;

namespace OSPF
{
class Area;
class OspfRib;

class FloodManager
{
public:
    explicit FloodManager(Area& area, ProcessQueue& sch);

    void enqueueFlood(LsaRecordRef& record, const FloodInfo& info);
    void enqueueFlood(LsaRecordRef&& record, const FloodInfo& info);

private:
    void startFloodTimer();

    void onFloodTimer();

    void runFlood();

private:
    Area& area;
    ProcessQueue& scheduler;
    FloodQueue fq;

    std::atomic<bool> timerActive{0};
    uint32_t timerId;
};
}

#endif // FLOOD_MANAGER_H
