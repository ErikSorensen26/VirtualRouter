// InterfaceTimers.h

#ifndef EIGRP_TIMER_MANAGER_H
#define EIGRP_TIMER_MANAGER_H

#include <IPAddress.h>

namespace core { class Global; class ProcessQueue; }

namespace routing::eigrp
{
struct OutgoingQuery;
class DuelEngine;
struct TopologyEntry;
class Eigrp;
class Neighbor;

class TimerManager
{
public:
    TimerManager(Eigrp& base, core::ProcessQueue& scheduler);

    void startSIATimer(OutgoingQuery& entry, Neighbor& neighbor);
    void cancelSIATimer(OutgoingQuery& entry);

private:

    Eigrp& base;
    core::ProcessQueue& scheduler;
};
} // namespace routing

#endif // EIGRP_TIMER_MANAGER_H

