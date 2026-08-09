// TimerManager.cpp

#include <ControlScheduler.h>

#include "TimerManager.h"
#include "eigrp/core/Eigrp.h"
#include "DualEngine.h"

namespace routing::eigrp
{
TimerManager::TimerManager(Eigrp& base, core::ProcessQueue& scheduler)
    : base(base), scheduler(scheduler.ref())
{}

void TimerManager::startSIATimer(OutgoingQuery& query, Neighbor& neighbor)
{
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(base.getGlobalConfigMgr().getSIATime());

    // Schedule SIA-Query timer
    query.siaTimerId = scheduler.postAfter(expirationTime, [
        this, queryPtr = &query, neighborPtr = &neighbor
    ](uint32_t){
        base.getTopology().handleSIATimeout(*queryPtr, *neighborPtr);
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
