// EigrpInterfaceTimerManager.cpp

#include "TimerManager.h"
#include <TimeManager.h>
#include "ReliableTransport.h"
#include <VirtualRouter.h>
#include <Global.h>
#include "ReliablePacket.hpp"
#include "EigrpInterface.h"
#include <EigrpTypes.hpp>
#include <Eigrp.h>
#include <DuelEngine.h>

#define VALIDATION_CAPTURES  \
    vrf = vrf->instanceName, \
    global = &vrf->global,   \
    as = base->getAS(),      \
    af = base->getAF()       \

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

void InterfaceTimers::startHello()
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    // Mark hello as active
    if (helloTimerActive.load(std::memory_order_relaxed) && helloTimerId.load(std::memory_order_relaxed) != 0)
        return; // Timer already active
    
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);
        if (helloStartTime.time_since_epoch().count() == 0)
        {
            helloStartTime = std::chrono::steady_clock::now();
        }

        auto nextExpiration = std::chrono::steady_clock::now() + std::chrono::seconds(iface.configs->helloTime.load(std::memory_order_relaxed));

        auto* vrf = base->routingInstance;
        uint32_t helloId = tmgr.addTimer(nextExpiration, [
            &, VALIDATION_CAPTURES
        ](){
            if (InterfaceTimers::validateProcess(vrf, as, af, global))
            {
                helloTimerId = 0;
                handleHelloReschedule();
            }
        });
        helloTimerId.store(helloId, std::memory_order_release);
    }
}

void InterfaceTimers::startHelloHelper()
{
    if (helloTimerActive.exchange(true))
    {
        // Hello timer is already active
        return;
    }
    helloTimerActive.store(true, std::memory_order_release);

    sendHello();
    startHello();
}

void InterfaceTimers::stopHello()
{
    helloTimerActive.store(false, std::memory_order_release);
    if (helloTimerId != 0)
    {
        tmgr.cancelTimer(helloTimerId);
        if (!helloDone.load(std::memory_order_relaxed))
        {
            // Wait until the hello timer is fully shut down
            while (!helloDone.load(std::memory_order_relaxed))
            {
                std::this_thread::yield();
            }
        }
        helloTimerId = 0;
    }
    iface.getNTable().cancelAllHoldTimers();
}

void InterfaceTimers::handleHelloReschedule()
{
    if (!helloTimerActive.load(std::memory_order_relaxed))
        return;
    helloDone.store(false, std::memory_order_release);
    try
    {
        sendHello();
    }
    catch (...)

    // Reset and reschedule Hello timer
    {
        std::lock_guard<std::mutex> lock(helloTimerMutex);
        helloTimerId = 0; // Clear timer ID after packet is sent
        helloStartTime = std::chrono::steady_clock::now();
    }

    // Mark hello as done
    helloDone.store(true, std::memory_order_release);
    startHello(); // Reschedule
}

void InterfaceTimers::sendHello()
{
    auto& rtp = iface.getRtp();
    auto unicastNeighbors = iface.getNTable().lookupUnicast();
    for (const auto& neighbor : unicastNeighbors)
    {
        rtp.sendUnicastHello(neighbor->ipAddress);
    }
    if (iface.configs->multicastEnabled.load(std::memory_order_relaxed))
    {
        rtp.sendHello();
    }
}

void InterfaceTimers::startHoldTimer(Neighbor& neighbor)
{
    cancelHoldTimer(neighbor);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(neighbor.holdTime.load(std::memory_order_relaxed));
    neighbor.holdTimerId.store(tmgr.addTimer(expirationTime, [this, nbr = &neighbor](){
        handleHoldTimeExpire(*nbr);
    }), std::memory_order_release);
}

void InterfaceTimers::cancelHoldTimer(Neighbor& neighbor)
{
    if (neighbor.holdTimerId.load(std::memory_order_relaxed) != 0)
    {
        tmgr.cancelTimer(neighbor.holdTimerId);
        neighbor.holdTimerId.load(std::memory_order_release);
    }
}

void InterfaceTimers::restartHoldTimer(Neighbor& neighbor)
{
    cancelHoldTimer(neighbor);
    startHoldTimer(neighbor);
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
    info.timerId = tmgr.addTimer(expirationTime, [this, neighbor, m = &multicast, i = &info, seq]() {
        iface.getRtp().handleRetransmission(neighbor, *m, *i, seq);
    });
}

void InterfaceTimers::startRetransmissionTimer(Neighbor* neighbor, UnicastReliablePacket& unicast, uint32_t seq)
{
    double timeout = neighbor->rto;
    if (unicast.info.timerId != 0) tmgr.cancelTimer(unicast.info.timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout * 1000));
    unicast.info.timerId = tmgr.addTimer(expirationTime, [this, neighbor, u = &unicast, seq]() {
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
    uint32_t gracefulTimerId = tmgr.addTimer(expireTime, [this, nbr = &neighbor]() {
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

bool InterfaceTimers::validateProcess(const std::string& vrfname, uint32_t as, const AddressFamily& af, Global* global)
{
    if (VirtualRouter* virtualRouter = global->getRoutingInstance(vrfname))
    {
        if (auto* eigrp = virtualRouter->getEigrpAutonomousSystem(as))
        {
            if (af == AddressFamily::IPv4 ? !eigrp->ipv4 : !eigrp->ipv6)
            {
                return false;
            }
        }
        else return false;
    }
    else return false;
    return true;
}

void InterfaceTimers::restartDampeningResetTimer()
{
    if (auto id = dampeningResetId.load(std::memory_order_relaxed); id != 0)
        tmgr.cancelTimer(id);

    suppressedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getDampeningResetTime()),
    dampeningResetId.store(tmgr.addTimer(
        suppressedUntil,
        [&]() { iface.onDampeningResetExpire(); }
    ), std::memory_order_release);
}

void InterfaceTimers::restartDampeningRestartTimer()
{
    if (auto id = dampeningRestartId.load(std::memory_order_relaxed); id != 0)
        tmgr.cancelTimer(id);

    dampeningRestartId.store(tmgr.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(base->getGlobalConfigMgr().getDampeningRestart()),
        [&]() { iface.onDampeningRestartExpire(); }
    ), std::memory_order_release);
}

void InterfaceTimers::startDampeningIntervalTimer()
{
    if (auto id = dampeningIntervalId.load(std::memory_order_relaxed); id != 0)
        tmgr.cancelTimer(id);

    auto dampeningTime = iface.configs->dampeningIntervalConfigured.load(std::memory_order_relaxed)
        ? iface.configs->dampeningInterval.load(std::memory_order_relaxed)
        : iface.getBase().getGlobalConfigMgr().getDampeningInterval();
    dampeningIntervalId.store(tmgr.addTimer(
        std::chrono::steady_clock::now() + std::chrono::seconds(dampeningTime),
        [&]() { iface.onDampeningIntervalExpire(); }
    ), std::memory_order_release);
}
}
