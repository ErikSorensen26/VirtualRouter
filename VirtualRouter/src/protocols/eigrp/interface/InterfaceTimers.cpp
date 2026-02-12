// EigrpInterfaceTimerManager.cpp

#include <TimeManager.h>

#include "InterfaceTimers.h"
#include "eigrp/topology/TimerManager.h"
#include "EigrpInterface.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/rtp/ReliableTransport.h"

namespace Eigrp
{
InterfaceTimers::InterfaceTimers(EigrpInterface& iface, TimeManager& tmgr) : iface(iface), tmgr(tmgr)
{
    base = &iface.getBase();
}

InterfaceTimers::~InterfaceTimers()
{
    stopHello();
}

void InterfaceTimers::scheduleHello()
{
    if (iface.configs.isPassive.load(std::memory_order_relaxed)) return;
    // Mark hello as active
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
        return; // Timer already active
    
    auto nextExpiration = std::chrono::steady_clock::now() + std::chrono::seconds(iface.configs.helloTime.load(std::memory_order_relaxed));

    uint32_t helloId = tmgr.addTimer(nextExpiration, [this](uint32_t){
        helloTimerId.store(0, std::memory_order_release);
        startHello();
    });
    helloTimerId.store(helloId, std::memory_order_release);
}

void InterfaceTimers::stopHello()
{
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
    {
        tmgr.cancelTimer(helloTimerId);
        helloTimerId.store(0, std::memory_order_release);
    }
    iface.getNTable().cancelAllHoldTimers();
}

void InterfaceTimers::startHello()
{
    sendHello();
    scheduleHello(); // Reschedule
}

void InterfaceTimers::sendHello()
{
    auto& rtp = iface.getRtp();
    auto unicastNeighbors = iface.getNTable().lookupUnicast();
    for (const auto& neighbor : unicastNeighbors)
    {
        rtp.sendUnicastHello(neighbor->ipAddress);
    }
    if (iface.configs.multicastEnabled.load(std::memory_order_relaxed))
    {
        rtp.sendHello();
    }
}

void InterfaceTimers::startHoldTimer(Neighbor& neighbor)
{
    cancelHoldTimer(neighbor);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(neighbor.holdTime.load(std::memory_order_relaxed));
    neighbor.holdTimerId.store(tmgr.addTimer(expirationTime, [this, nbr = &neighbor](uint32_t){
        handleHoldTimeExpire(*nbr);
    }), std::memory_order_release);
}

void InterfaceTimers::cancelHoldTimer(Neighbor& neighbor)
{
    if (auto tid = neighbor.holdTimerId.load(std::memory_order_relaxed); tid != 0)
    {
        tmgr.cancelTimer(tid);
        neighbor.holdTimerId.store(0, std::memory_order_release);
    }
}

void InterfaceTimers::handleHoldTimeExpire(Neighbor& neighbor)
{
    iface.getRtp().pendingPeerTermination.store(true, std::memory_order_release);
    if (base->getGlobalConfigMgr().isNonStopForwarding())
        iface.getNTable().startGracefulRestart(neighbor);
    else
        iface.getNTable().onDown(neighbor);
}

void InterfaceTimers::startRetransmissionTimer(Neighbor* neighbor, MulticastReliablePacket& multicast, ReliableInfo& info, uint32_t seq)
{
    double timeout = neighbor->rto;
    if (info.timerId != 0) tmgr.cancelTimer(info.timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
    info.timerId = tmgr.addTimer(expirationTime, [this, neighbor, m = &multicast, i = &info, seq](uint32_t) {
        iface.getRtp().handleRetransmission(neighbor, *m, *i, seq);
    });
}

void InterfaceTimers::startRetransmissionTimer(Neighbor* neighbor, UnicastReliablePacket& unicast, uint32_t seq)
{
    double timeout = neighbor->rto;
    if (unicast.info.timerId != 0) tmgr.cancelTimer(unicast.info.timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
    unicast.info.timerId = tmgr.addTimer(expirationTime, [this, neighbor, u = &unicast, seq](uint32_t) {
        iface.getRtp().handleRetransmission(neighbor, *u, seq);
    });
}

void InterfaceTimers::cancelRetransmissionTimer(ReliableInfo& pkt)
{
    if (pkt.timerId != 0)
    {
        tmgr.cancelTimer(pkt.timerId);
        pkt.timerId = 0;
    }
}

void InterfaceTimers::startGracefulTimer(Neighbor& neighbor)
{
    auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getPurgeTime());
    uint32_t gracefulTimerId = tmgr.addTimer(expireTime, [this, nbr = &neighbor](uint32_t) {
        iface.getNTable().onDown(*nbr);
    });
    neighbor.gracefulTimerId.store(gracefulTimerId, std::memory_order_release);
    neighbor.isGraceful = true;
}

void InterfaceTimers::cancelGracefulTimer(Neighbor& neighbor)
{
    if (neighbor.gracefulTimerId != 0)
    {
        tmgr.cancelTimer(neighbor.gracefulTimerId);
        neighbor.gracefulTimerId = 0;
        neighbor.isGraceful = false;
    }
}

void InterfaceTimers::cancelNeighborTimers(Neighbor& neighbor)
{
    cancelGracefulTimer(neighbor);
    cancelHoldTimer(neighbor);
}

void InterfaceTimers::restartDampeningResetTimer()
{
    if (auto id = dampeningResetId.load(std::memory_order_relaxed); id != 0)
        tmgr.cancelTimer(id);

    suppressedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getDampeningResetTime()),
    dampeningResetId.store(tmgr.addTimer(
        suppressedUntil,
        [this](uint32_t) { iface.onDampeningResetExpire(); }
    ), std::memory_order_release);
}

void InterfaceTimers::restartDampeningRestartTimer()
{
    if (auto id = dampeningRestartId.load(std::memory_order_relaxed); id != 0)
        tmgr.cancelTimer(id);

    dampeningRestartId.store(tmgr.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getDampeningRestart()),
        [this](uint32_t) { iface.onDampeningRestartExpire(); }
    ), std::memory_order_release);
}

void InterfaceTimers::startDampeningIntervalTimer()
{
    if (auto id = dampeningIntervalId.load(std::memory_order_relaxed); id != 0)
        tmgr.cancelTimer(id);

    auto dampeningTime = iface.configs.dampeningIntervalConfigured.load(std::memory_order_relaxed)
        ? iface.configs.dampeningInterval.load(std::memory_order_relaxed)
        : iface.getBase().getGlobalConfigMgr().getDampeningInterval();
    dampeningIntervalId.store(tmgr.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(dampeningTime),
        [this](uint32_t) { iface.onDampeningIntervalExpire(); }
    ), std::memory_order_release);
}
}
