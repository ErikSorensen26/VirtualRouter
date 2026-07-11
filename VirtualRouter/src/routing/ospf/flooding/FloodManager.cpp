// FloodManager.cpp

#include <TimeManager.h>

#include "FloodManager.h"
#include "ospf/area/Area.h"
#include "configs/registry/router/OspfRegistry.h"
#include "ospf/interface/OspfInterface.h"

namespace routing::ospf
{
FloodManager::FloodManager(Area& area)
    : area(area), fq(1024)
{}

void FloodManager::enqueueFlood(LsaRecordRef& record, const FloodInfo& info)
{
    bool wasEmpty = fq.empty();
    fq.enqueue(record, info);

    if (wasEmpty)
        startFloodTimer();
}

void FloodManager::enqueueFlood(LsaRecordRef&& record, const FloodInfo& info)
{
    bool wasEmpty = fq.empty();
    fq.enqueue(record, info);

    if (wasEmpty)
        startFloodTimer();
}

void FloodManager::startFloodTimer()
{
    if (timerActive.load(std::memory_order_relaxed))
        return;

    uint32_t pacingMs = area.getProcessConfigs().get<config::Ospf::FLOOD_PACING>().load();

    auto fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(pacingMs);

    timerId = area.scheduler.postAfter(fireTime, [this](uint32_t) {
        onFloodTimer();
    });
}

void FloodManager::onFloodTimer()
{
    runFlood();
}

void FloodManager::runFlood()
{
    auto batch = fq.tryDequeueBatch();
    if (batch.empty())
        return;

    area.send(batch);
}
} // namespace routing
