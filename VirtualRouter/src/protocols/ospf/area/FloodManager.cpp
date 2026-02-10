// FloodManager.cpp

#include "FloodManager.h"
#include "OspfArea.h"
#include <OspfProcess.h>
#include <TimeManager.h>
#include <OspfInterface.h>

namespace OSPF
{
FloodManager::FloodManager(OspfArea& area, ProcessQueue& sch)
    : area(area), scheduler(sch), fq(0)
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

    uint32_t pacingMs = area.process().getConfigs().get<Config::Ospf::FLOOD_PACING>().load();

    auto fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(pacingMs);

    timerId = scheduler.schedule(fireTime, [this](uint32_t) {
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

    auto& ifaceMgr = area.process().getIfaceMgr();

    for (auto& [id, iface] : ifaceMgr.ospfInterfaceList)
    {
        if (id.area !=  area.areaId)
            continue;

        if (iface.getConfigs().get<Config::OspfInterface::DATABASE_FILTER>().load())
            continue;

        area.send(iface, batch);
    }
}
}
