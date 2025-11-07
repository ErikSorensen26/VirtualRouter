// TimerManager.cpp

#include "TimerManager.h"
#include <EigrpCore.h>
#include <TimeManager.h>
#include "DuelEngine.h"

namespace Eigrp
{
TimerManager::TimerManager(Eigrp& base, TimeManager& tmgr)
    : base(base), tmgr(tmgr)
{
    duel = &base.getDuel();
}

void TimerManager::startSIATimer(OutgoingQuery& query, Neighbor& neighbor)
{
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(base.getGlobalConfigMgr().getSIATime());

    // Schedule SIA-Query timer
    query.siaTimerId = tmgr.addTimer(expirationTime, [
        this, queryPtr = &query, neighborPtr = &neighbor
    ](){
        base.getDuel().handleSIATimeout(*queryPtr, *neighborPtr);
    });
}

void TimerManager::cancelSIATimer(OutgoingQuery& query)
{
    if (query.siaTimerId != 0)
    {
        tmgr.cancelTimer(query.siaTimerId);
        query.siaTimerId = 0;
    }
}
}
