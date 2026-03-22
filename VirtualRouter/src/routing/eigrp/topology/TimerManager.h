// InterfaceTimers.h

#ifndef EIGRP_TIMER_MANAGER_H
#define EIGRP_TIMER_MANAGER_H

#include <IPAddress.h>

class Global;
class ProcessQueue;

namespace EIGRP
{
struct OutgoingQuery;
class DuelEngine;
struct TopologyEntry;
class Eigrp;
class Neighbor;

class TimerManager
{
public:
    TimerManager(Eigrp& base, ProcessQueue& scheduler);

    void startSIATimer(OutgoingQuery& entry, Neighbor& neighbor);
    void cancelSIATimer(OutgoingQuery& entry);

private:

    Eigrp& base;
    ProcessQueue& scheduler;
};
}

#endif // EIGRP_TIMER_MANAGER_H
