// OspfInterfaceTimers.cpp

#include <OspfInterfaceTimers.h>
#include <OspfNeighbor.h>
#include <OspfInterface.h>
#include <TimeManager.h>

namespace OSPF
{
InterfaceTimers::InterfaceTimers(TimeManager& tm, OspfInterface& iface)
    : tmgr(tm), iface(iface)
{}

void InterfaceTimers::restartInactiveTimer(Neighbor& nbr)
{
}

void InterfaceTimers::startDbdRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.configs->retransmitInterval.load(std::memory_order_relaxed);
    uint32_t timerId = nbr.getRtr().dbdTimerId.load(std::memory_order_relaxed);
    if (timerId != 0) tmgr.cancelTimer(timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    timerId = tmgr.addTimer(expirationTime, [this, &nbr]() {
        iface.getDispatcher().retransmitDbd(nbr);
    });
    nbr.getRtr().dbdTimerId.store(timerId, std::memory_order_release);
}

void InterfaceTimers::startLsrRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.configs->retransmitInterval.load(std::memory_order_relaxed);
    uint32_t timerId = nbr.getRtr().lsrTimerId.load(std::memory_order_relaxed);
    if (timerId != 0) tmgr.cancelTimer(timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    timerId = tmgr.addTimer(expirationTime, [this, &nbr]() {
        iface.getDispatcher().retransmitLsr(nbr);
    });
}

void InterfaceTimers::startLsuRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.configs->retransmitInterval.load(std::memory_order_relaxed);
    uint32_t timerId = nbr.getRtr().lsuTimerId.load(std::memory_order_relaxed);
    if (timerId != 0) tmgr.cancelTimer(timerId);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    timerId = tmgr.addTimer(expirationTime, [this, &nbr]() {
        iface.getDispatcher().retransmitLsu(nbr);
    });
    nbr.getRtr().lsuTimerId.store(timerId, std::memory_order_release);
}
}
