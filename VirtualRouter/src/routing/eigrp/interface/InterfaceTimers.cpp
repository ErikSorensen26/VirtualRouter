// EigrpInterfaceTimerManager.cpp

#include <ControlScheduler.h>

#include "InterfaceTimers.h"
#include "eigrp/topology/TimerManager.h"
#include "EigrpInterface.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/rtp/ReliableTransport.h"

namespace routing::eigrp
{
InterfaceTimers::InterfaceTimers(EigrpInterface& iface, core::ProcessQueue& scheduler) : iface(iface), scheduler(scheduler)
{
    base = &iface.getBase();
}

InterfaceTimers::~InterfaceTimers()
{
    stopHello();
}

void InterfaceTimers::scheduleHello()
{
    if (iface.configs.reg.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;
    // Mark hello as active
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
        return; // Timer already active
    
    auto nextExpiration = std::chrono::steady_clock::now() + std::chrono::seconds(iface.configs.reg.get<config::EigrpInterface::HELLO_INTERVAL>().load());

    uint32_t helloId = scheduler.schedule(nextExpiration, [this](uint32_t){
        helloTimerId.store(0, std::memory_order_release);
        startHello();
    });
    helloTimerId.store(helloId, std::memory_order_release);
}

void InterfaceTimers::stopHello()
{
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
    {
        scheduler.cancel(helloTimerId);
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
    if (iface.multicastEnabledFlag.load(std::memory_order_relaxed))
    {
        rtp.sendHello();
    }
}

void InterfaceTimers::startHoldTimer(Neighbor& neighbor)
{
    cancelHoldTimer(neighbor);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(neighbor.holdTime.load(std::memory_order_relaxed));
    neighbor.holdTimerId.store(scheduler.schedule(expirationTime, [this, nbr = &neighbor](uint32_t){
        handleHoldTimeExpire(*nbr);
    }), std::memory_order_release);
}

void InterfaceTimers::cancelHoldTimer(Neighbor& neighbor)
{
    if (auto tid = neighbor.holdTimerId.load(std::memory_order_relaxed); tid != 0)
    {
        scheduler.cancel(tid);
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
    if (info.timerId != 0) scheduler.cancel(info.timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
    info.timerId = scheduler.schedule(expirationTime, [this, neighbor, m = &multicast, i = &info, seq](uint32_t) {
        iface.getRtp().handleRetransmission(neighbor, *m, *i, seq);
    });
}

void InterfaceTimers::startRetransmissionTimer(Neighbor* neighbor, UnicastReliablePacket& unicast, uint32_t seq)
{
    double timeout = neighbor->rto;
    if (unicast.info.timerId != 0) scheduler.cancel(unicast.info.timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
    unicast.info.timerId = scheduler.schedule(expirationTime, [this, neighbor, u = &unicast, seq](uint32_t) {
        iface.getRtp().handleRetransmission(neighbor, *u, seq);
    });
}

void InterfaceTimers::cancelRetransmissionTimer(ReliableInfo& pkt)
{
    if (pkt.timerId != 0)
    {
        scheduler.cancel(pkt.timerId);
        pkt.timerId = 0;
    }
}

void InterfaceTimers::startGracefulTimer(Neighbor& neighbor)
{
    auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getPurgeTime());
    uint32_t gracefulTimerId = scheduler.schedule(expireTime, [this, nbr = &neighbor](uint32_t) {
        iface.getNTable().onDown(*nbr);
    });
    neighbor.gracefulTimerId.store(gracefulTimerId, std::memory_order_release);
    neighbor.isGraceful = true;
}

void InterfaceTimers::cancelGracefulTimer(Neighbor& neighbor)
{
    if (neighbor.gracefulTimerId != 0)
    {
        scheduler.cancel(neighbor.gracefulTimerId);
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
        scheduler.cancel(id);

    suppressedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getDampeningResetTime()),
    dampeningResetId.store(scheduler.schedule(
        suppressedUntil,
        [this](uint32_t) { iface.onDampeningResetExpire(); }
    ), std::memory_order_release);
}

void InterfaceTimers::restartDampeningRestartTimer()
{
    if (auto id = dampeningRestartId.load(std::memory_order_relaxed); id != 0)
        scheduler.cancel(id);

    dampeningRestartId.store(scheduler.schedule(
        std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getDampeningRestart()),
        [this](uint32_t) { iface.onDampeningRestartExpire(); }
    ), std::memory_order_release);
}

void InterfaceTimers::startDampeningIntervalTimer()
{
    if (auto id = dampeningIntervalId.load(std::memory_order_relaxed); id != 0)
        scheduler.cancel(id);

    if (!iface.configs.reg.get<config::EigrpInterface::DAMPENING_INTERVAL>().load())
        return;
    auto dampeningTime = iface.configs.reg.get<config::EigrpInterface::DAMPENING_INTERVAL_TIME>().load();
    dampeningIntervalId.store(scheduler.schedule(
        std::chrono::steady_clock::now() + std::chrono::seconds(dampeningTime),
        [this](uint32_t) { iface.onDampeningIntervalExpire(); }
    ), std::memory_order_release);
}
} // namespace routing
