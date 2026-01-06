// OspfInterfaceTimers.h

#ifndef OSPF_INTERFACE_TIMERS_H
#define OSPF_INTERFACE_TIMERS_H

#include <OspfPacket.hpp>

class TimeManager;

namespace OSPF
{
class Neighbor;
class OspfInterface;
class InterfaceTimers
{
public:
    InterfaceTimers(TimeManager& tm, OspfInterface& iface);

    void restartInactiveTimer(Neighbor& nbr);

    void startDbdRetransmissionTimer(Neighbor& neighbor);
    void startLsrRetransmissionTimer(Neighbor& neighbor);
    void startLsuRetransmissionTimer(Neighbor& neighbor);

private:
    TimeManager& tmgr;
    OspfInterface& iface;
};
}

#endif // OSPF_INTERFACE_TIMERS_H
