// OspfInterfaceTimers.cpp

#include <OspfInterfaceTimers.h>
#include <OspfNeighbor.h>
#include <OspfInterface.h>
#include <TimeManager.h>
#include <OspfProcess.h>
#include <PacketDispatcher.h>

namespace OSPF
{
InterfaceTimers::InterfaceTimers(TimeManager& tm, OspfInterface& iface)
    : tmgr(tm), iface(iface)
{
    stopHello();
}

void InterfaceTimers::scheduleHello()
{
    if (iface.getConfigs().get<Config::OspfInterface::PASSIVE>().load()) return;
    // Mark hello as active
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
        return; // Timer already active
    {
        if (helloStartTime.time_since_epoch().count() == 0)
        {
            helloStartTime = std::chrono::steady_clock::now();
        }

        auto nextExpiration = std::chrono::steady_clock::now() + iface.helloTime.load(std::memory_order_relaxed);

        uint32_t timerId = tmgr.addTimer(nextExpiration, [this](uint32_t) {
            helloTimerId.store(0, std::memory_order_release);
            startHello();
        });
        helloTimerId.store(timerId, std::memory_order_release);
    }
}

void InterfaceTimers::stopHello()
{
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
    {
        tmgr.cancelTimer(helloTimerId);
        helloTimerId.store(0, std::memory_order_relaxed);
    }
    iface.getNTable().cancelAllInactiveTimers();
}

void InterfaceTimers::startHello()
{
    sendHello();
    scheduleHello();
}

void InterfaceTimers::sendHello()
{
    auto& dispatcher = iface.getDispatcher();
    if (iface.isMulticast.load(std::memory_order_relaxed))
        dispatcher.sendHello(); // Send multicast hello
    else
    {
        // Send unicast hello for each neighbor
        auto& ntable = iface.getNTable();
        for (auto& [_, nbr] : ntable.neighbors)
            if (nbr.unicast)
                dispatcher.sendUnicastHello(nbr);
    }
}

void InterfaceTimers::startInactiveTimer(Neighbor& neighbor)
{
    cancleInactiveTimer(neighbor);
    auto expirationTime = std::chrono::steady_clock::now() + iface.deadTime.load(std::memory_order_relaxed);
    neighbor.inactivityTimerId.store(tmgr.addTimer(expirationTime,
        [this, nbr = &neighbor](uint32_t) {
            handleInactiveTimeExpire(*nbr);
        }), std::memory_order_release
    );
}

void InterfaceTimers::cancleInactiveTimer(Neighbor& neighbor)
{
    if (auto tid = neighbor.inactivityTimerId.load(std::memory_order_relaxed); tid != 0)
    {
        tmgr.cancelTimer(tid);
        neighbor.inactivityTimerId.store(0, std::memory_order_release);
    }
}

void InterfaceTimers::handleInactiveTimeExpire(Neighbor& neighbor)
{
    neighbor.setState(Neighbor::State::DOWN);
}

void InterfaceTimers::startDbdRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.getConfigs().get<Config::OspfInterface::RETRANSMIT_INTERVAL>().load();

    uint32_t& tid = nbr.getRtr().dbdTimerId;
    if (tid != 0) tmgr.cancelTimer(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    tid = tmgr.addTimer(expirationTime, [this, &nbr](uint32_t)
    {
        iface.getDispatcher().onDbdRetransmissionTimer(nbr);
    });
}

void InterfaceTimers::startLsrRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.getConfigs().get<Config::OspfInterface::RETRANSMIT_INTERVAL>().load();

    uint32_t& tid = nbr.getRtr().lsrs().retransmitTimerId;
    if (tid != 0) tmgr.cancelTimer(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    tid = tmgr.addTimer(expirationTime, [this, &nbr](uint32_t)
    {
        iface.getDispatcher().onLsrRetransmissionTimer(nbr);
    });
}

void InterfaceTimers::startLsuRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.getConfigs().get<Config::OspfInterface::RETRANSMIT_INTERVAL>().load();

    uint32_t& tid = nbr.getRtr().lsus().retransmitTimerId;
    if (tid != 0) tmgr.cancelTimer(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    tid = tmgr.addTimer(expirationTime, [this, &nbr](uint32_t)
    {
        iface.getDispatcher().onLsuRetransmissionTimer(nbr);
    });
}

void InterfaceTimers::startLsrPacingTimer(Neighbor& nbr)
{
    uint8_t timeout = iface.getProcess().getConfigs().get<Config::Ospf::RETRANSMISSION_PACING>().load();

    uint32_t& tid = nbr.getRtr().lsrs().pacingTimerId;
    if (tid != 0) tmgr.cancelTimer(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout));
    tid = tmgr.addTimer(expirationTime, [this, &nbr](uint32_t)
    {
        iface.getDispatcher().onLsrPacingTimer(nbr);
    });
}

void InterfaceTimers::startLsuPacingTimer(Neighbor* nbr)
{
    uint8_t timeout = iface.getProcess().getConfigs().get<Config::Ospf::RETRANSMISSION_PACING>().load();

    uint32_t& tid = nbr ? nbr->getRtr().lsrs().pacingTimerId : iface.getDispatcher().getMulticastLsu().pacingTimerId;
    if (tid != 0) tmgr.cancelTimer(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout));
    tid = tmgr.addTimer(expirationTime, [this, nbr](uint32_t)
    {
        iface.getDispatcher().onLsuPacingTimer(nbr);
    });
}
}
