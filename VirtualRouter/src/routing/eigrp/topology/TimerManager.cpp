// TimerManager.cpp

#include <ControlScheduler.h>

#include "TimerManager.h"
#include "eigrp/core/Eigrp.h"
#include "DualEngine.h"
#include "configs/FieldAccessor.hpp"

namespace routing::eigrp
{
TimerManager::TimerManager(Eigrp& process, core::ProcessQueue& scheduler)
    : process(process), scheduler(scheduler.ref())
{}

void TimerManager::startSIATimer(OutgoingQuery& query, Neighbor& neighbor)
{
    if (process.getConfigs().get<config::Eigrp::ACTIVE_DISABLED>().load())
        return;

    auto siaTimeField = process.getConfigs().get<config::Eigrp::ACTIVE_TIME>();
    uint16_t siaTime = siaTimeField.hasValue() ? siaTimeField.load() : 90;
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(siaTime);

    // Schedule SIA-Query timer
    query.siaTimerId = scheduler.postAfter(expirationTime, [
        this, queryPtr = &query, neighborPtr = &neighbor
    ](uint32_t){
        process.getTopology().handleSIATimeout(*queryPtr, *neighborPtr);
    });
}

void TimerManager::cancelSIATimer(OutgoingQuery& query)
{
    if (query.siaTimerId != 0)
    {
        scheduler.cancel(query.siaTimerId);
        query.siaTimerId = 0;
    }
}
} // namespace routing
