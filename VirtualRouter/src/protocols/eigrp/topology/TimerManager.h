// InterfaceTimers.h

#ifndef EIGRP_TIMER_MANAGER_H
#define EIGRP_TIMER_MANAGER_H

#include <IPAddress.h>

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
};
}

#endif // EIGRP_TIMER_MANAGER_H
