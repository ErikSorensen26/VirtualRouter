// InterfaceTimers.h

#ifndef EIGRP_INTERFACE_TIMERS_H
#define EIGRP_INTERFACE_TIMERS_H

#include <cstdint>
#include <chrono>
#include <atomic>
#include <IPAddress.h>

class Global;
class ProcessQueue;
class Internal_EigrpTest;
struct StaticHeader;
namespace EIGRP
{
struct OutgoingQuery;
}

namespace EIGRP
{
class EigrpInterface;
class Neighbor;
class ReliablePacket;
class Eigrp;
class MulticastReliablePacket;
class UnicastReliablePacket;
struct ReliableInfo;

class InterfaceTimers
{
public:
    friend class ::Internal_EigrpTest;
    InterfaceTimers(EigrpInterface& iface, ProcessQueue& scheduler);
    ~InterfaceTimers();

    // Hello
    void startHello();
    void scheduleHello();
    void stopHello();
    void sendHello();

    // Hold
    void startHoldTimer(Neighbor& neighbor);
    void cancelHoldTimer(Neighbor& neighbor);
    void handleHoldTimeExpire(Neighbor& neighbor);

    // Retransmission
    void startRetransmissionTimer(Neighbor* neighbor, MulticastReliablePacket& multicast, ReliableInfo& info, uint32_t seq);
    void startRetransmissionTimer(Neighbor* neighbor, UnicastReliablePacket& unicast, uint32_t seq);
    void cancelRetransmissionTimer(ReliableInfo& pkt);

    void cancelNeighborTimers(Neighbor&);
    void startGracefulTimer(Neighbor& neighbor);
    void cancelGracefulTimer(Neighbor& neighbor);

    void restartDampeningResetTimer();
    void restartDampeningRestartTimer();
    void startDampeningIntervalTimer();

    bool isDampenExpired() { return std::chrono::steady_clock::now() >= suppressedUntil; }

    static bool validateProcess(const std::string& vrfname, uint32_t as, const AddressFamily& af, Global* global);

private:

    // Hello timer
    std::atomic<uint32_t> helloTimerId = 0; ///< Timer ID for the Hello timer.
    std::chrono::steady_clock::time_point helloStartTime; ///< Start time for the Hello timer.

    // Other options/tracking
    std::chrono::steady_clock::time_point suppressedUntil;
    std::atomic<uint32_t> dampeningResetId{0}, dampeningRestartId{0}, dampeningIntervalId{0};

    std::atomic<bool> runTimers = true; ///< Flag to indicate if timers should continue running.

    Eigrp* base;
    EigrpInterface& iface;
    ProcessQueue& scheduler;
};
}

#endif // EIGRP_TIMER_MANAGER_H
