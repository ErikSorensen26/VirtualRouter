// OspfInterfaceTimers.h

#ifndef OSPF_INTERFACE_TIMERS_H
#define OSPF_INTERFACE_TIMERS_H

#include <OspfPacket.hpp>
#include <atomic>
#include <chrono>

class ProcessQueue;

namespace OSPF
{
class Neighbor;
class OspfInterface;
class InterfaceTimers
{
public:
    InterfaceTimers(ProcessQueue& pq, OspfInterface& iface);

    void scheduleHello(); 
    void startHello();
    void stopHello();
    void sendHello();

    void startInactiveTimer(Neighbor& neighbor);
    void cancleInactiveTimer(Neighbor& neighbor);
    void handleInactiveTimeExpire(Neighbor& neighbor);

    void startDbdRetransmissionTimer(Neighbor& neighbor);
    void startLsrRetransmissionTimer(Neighbor& neighbor);
    void startLsuRetransmissionTimer(Neighbor& neighbor);

    void startLsrPacingTimer(Neighbor& neighbor);
    void startLsuPacingTimer(Neighbor* neighbor);
    
private:
    // Hello timer
    std::atomic<uint32_t> helloTimerId{0};
    std::chrono::steady_clock::time_point helloStartTime;

    std::atomic<bool> runTimers = true;

    ProcessQueue& scheduler;
    OspfInterface& iface;
};
}

#endif // OSPF_INTERFACE_TIMERS_H
