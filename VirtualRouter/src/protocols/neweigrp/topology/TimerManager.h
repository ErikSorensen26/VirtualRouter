// InterfaceTimers.h

#ifndef EIGRP_TIMER_MANAGER_H
#define EIGRP_TIMER_MANAGER_H

#include <cstdint>
#include <IPAddress.hpp>
#include <RoutingTable.h>

class Global;
class TimeManager;

namespace Eigrp
{
struct OutgoingQuery;
class DuelEngine;
struct TopologyEntry;
class Eigrp;
class Neighbor;

class TimerManager
{
public:
    TimerManager(Eigrp& base, TimeManager& tmgr);

    void startSIATimer(OutgoingQuery& entry, Neighbor& neighbor);
    void cancelSIATimer(OutgoingQuery& entry);

private:

    Eigrp& base;
    TimeManager& tmgr;
    DuelEngine* duel;
};
}

#endif // EIGRP_TIMER_MANAGER_H
