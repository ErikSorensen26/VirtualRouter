// InterfaceTimers.h

#ifndef EIGRP_INTERFACE_TIMERS_H
#define EIGRP_INTERFACE_TIMERS_H

#include <cstdint>
#include <chrono>
#include <atomic>
#include <IPAddress.hpp>
#include <EigrpTypes.hpp>

class Global;
class TimeManager;
class Internal_EigrpTest;
struct StaticHeader;
namespace EigrpConfigs
{
struct OutgoingQuery;
}

namespace Eigrp
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
    InterfaceTimers(EigrpInterface& iface, TimeManager& tmgr);
    ~InterfaceTimers();

    void startHello();
    void startHelloHelper();
    void handleHelloReschedule();
    virtual void stopHello();
    virtual void startHoldTimer(Neighbor& neighbor);
    void cancelHoldTimer(Neighbor& neighbor);
    void restartHoldTimer(Neighbor& neighbor);
    void handleHoldTimeExpire(Neighbor& neighbor);
    void startRetransmissionTimer(Neighbor* neighbor, MulticastReliablePacket& multicast, ReliableInfo& info, uint32_t seq);
    void startRetransmissionTimer(Neighbor* neighbor, UnicastReliablePacket& unicast, uint32_t seq);
    void cancelRetransmissionTimer(ReliableInfo& pkt);
    void sendHello();

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
    std::mutex helloTimerMutex;
    std::atomic<uint32_t> helloTimerId = 0; ///< Timer ID for the Hello timer.
    std::atomic<bool>helloDone{true};
    std::atomic<bool>helloTimerActive = false; ///< Indicates if the Hello timer is active.
    std::chrono::steady_clock::time_point helloStartTime; ///< Start time for the Hello timer.

    // Other options/tracking
    std::chrono::steady_clock::time_point suppressedUntil;
    std::atomic<uint32_t> dampeningResetId{0}, dampeningRestartId{0}, dampeningIntervalId{0};

    std::atomic<bool>runTimers = true; ///< Flag to indicate if timers should continue running.

    Eigrp* base;
    EigrpInterface& iface;
    TimeManager& tmgr;
};
}

#endif // EIGRP_TIMER_MANAGER_H
