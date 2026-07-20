// InterfaceTimers.cpp

#include "InterfaceTimers.h"
#include "ospf/neighbor/Neighbor.h"
#include "OspfInterfaceBase.h"
#include "ospf/OspfProcess.h"
#include "ospf/transmission/PacketDispatcher.h"

namespace routing::ospf
{
InterfaceTimers::InterfaceTimers(OspfInterfaceBase& iface, core::ProcessQueue&& s)
    : scheduler(std::move(s)), iface(iface)
{
}

void InterfaceTimers::scheduleHello()
{
    if (iface.getPassive()) return;
    // Mark hello as active
    if (helloTimerId.load(std::memory_order_relaxed) != 0)
        return; // Timer already active
    {
        if (helloStartTime.time_since_epoch().count() == 0)
        {
            helloStartTime = std::chrono::steady_clock::now();
        }

        auto nextExpiration = std::chrono::steady_clock::now() + iface.getHelloInterval();

        uint32_t timerId = scheduler.postAfter(nextExpiration, [this](uint32_t) {
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
        scheduler.cancel(helloTimerId);
        helloTimerId.store(0, std::memory_order_relaxed);
    }
    iface.ntable.cancelAllInactiveTimers();
}

void InterfaceTimers::startHello()
{
    sendHello();
    scheduleHello();
}

void InterfaceTimers::sendHello()
{
    if (iface.getIsMulticast())
    {
        iface.dispatcher.sendHello(); // Send multicast hello
    }
    else // Send unicast hello for each neighbor
    {
        bool alwaysUnicast = iface.isVirtualLink();
        iface.ntable.forEach([this, alwaysUnicast](uint32_t, Neighbor& nbr) {
            if (nbr.unicast || alwaysUnicast)
                iface.dispatcher.sendUnicastHello(nbr);
        });
    }
}

void InterfaceTimers::startInactiveTimer(Neighbor& neighbor)
{
    cancleInactiveTimer(neighbor);
    auto expirationTime = std::chrono::steady_clock::now() + iface.getDeadInterval();
    neighbor.inactivityTimerId.store(scheduler.postAfter(expirationTime,
        [this, nbr = &neighbor](uint32_t) {
            handleInactiveTimeExpire(*nbr);
        }), std::memory_order_release
    );
}

void InterfaceTimers::cancleInactiveTimer(Neighbor& neighbor)
{
    if (auto tid = neighbor.inactivityTimerId.load(std::memory_order_relaxed); tid != 0)
    {
        scheduler.cancel(tid);
        neighbor.inactivityTimerId.store(0, std::memory_order_release);
    }
}

void InterfaceTimers::handleInactiveTimeExpire(Neighbor& neighbor)
{
    // RFC 3623 SS3: while helping this neighbor through a graceful restart, don't tear down the adjacency on a missed Hello -- just keep re-arming until the grace period ends.
    if (neighbor.helpingRestart.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() < neighbor.helperDeadline)
        {
            startInactiveTimer(neighbor);
            return;
        }
        neighbor.helpingRestart.store(false, std::memory_order_release);
    }

    neighbor.setState(Neighbor::State::DOWN);
}

void InterfaceTimers::startDbdRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.configsBase.get<config::OspfInterfaceBase::RETRANSMIT_INTERVAL>().load();

    uint32_t& tid = nbr.getRtr().dbdTimerId;
    if (tid != 0) scheduler.cancel(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    tid = scheduler.postAfter(expirationTime, [this, &nbr](uint32_t)
    {
        iface.dispatcher.onDbdRetransmissionTimer(nbr);
    });
}

void InterfaceTimers::startLsrRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.configsBase.get<config::OspfInterfaceBase::RETRANSMIT_INTERVAL>().load();

    uint32_t& tid = nbr.getRtr().lsrs().retransmitTimerId;
    if (tid != 0) scheduler.cancel(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    tid = scheduler.postAfter(expirationTime, [this, &nbr](uint32_t)
    {
        iface.dispatcher.onLsrRetransmissionTimer(nbr);
    });
}

void InterfaceTimers::startLsuRetransmissionTimer(Neighbor& nbr)
{
    uint16_t timeout = iface.configsBase.get<config::OspfInterfaceBase::RETRANSMIT_INTERVAL>().load();

    uint32_t& tid = nbr.getRtr().lsus().retransmitTimerId;
    if (tid != 0) scheduler.cancel(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int>(timeout));
    tid = scheduler.postAfter(expirationTime, [this, &nbr](uint32_t)
    {
        iface.dispatcher.onLsuRetransmissionTimer(nbr);
    });
}

void InterfaceTimers::startLsrPacingTimer(Neighbor& nbr)
{
    uint8_t timeout = iface.getProcessConfigs().get<config::Ospf::RETRANSMISSION_PACING>().load();

    uint32_t& tid = nbr.getRtr().lsrs().pacingTimerId;
    if (tid != 0) scheduler.cancel(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout));
    tid = scheduler.postAfter(expirationTime, [this, &nbr](uint32_t)
    {
        iface.dispatcher.onLsrPacingTimer(nbr);
    });
}

void InterfaceTimers::startLsuPacingTimer(Neighbor* nbr)
{
    uint8_t timeout = iface.getProcessConfigs().get<config::Ospf::RETRANSMISSION_PACING>().load();

    uint32_t& tid = nbr ? nbr->getRtr().lsus().pacingTimerId : iface.dispatcher.getMulticastLsu().pacingTimerId;
    if (tid != 0) scheduler.cancel(tid);

    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout));
    tid = scheduler.postAfter(expirationTime, [this, nbr](uint32_t)
    {
        iface.dispatcher.onLsuPacingTimer(nbr);
    });
}

void InterfaceTimers::cancelLsrTimers(Neighbor& nbr)
{
    auto& lsrs = nbr.getRtr().lsrs();

    if (lsrs.retransmitTimerId != 0)
    {
        scheduler.cancel(lsrs.retransmitTimerId);
        lsrs.retransmitTimerId = 0;
    }
    if (lsrs.pacingTimerId != 0)
    {
        scheduler.cancel(lsrs.pacingTimerId);
        lsrs.pacingTimerId = 0;
    }
}

void InterfaceTimers::cancelRetransmissionTimers(Neighbor& nbr)
{
    auto& rtr = nbr.getRtr();

    if (rtr.dbdTimerId != 0)
    {
        scheduler.cancel(rtr.dbdTimerId);
        rtr.dbdTimerId = 0;
    }

    cancelLsrTimers(nbr);

    if (rtr.lsus().retransmitTimerId != 0)
    {
        scheduler.cancel(rtr.lsus().retransmitTimerId);
        rtr.lsus().retransmitTimerId = 0;
    }
    if (rtr.lsus().pacingTimerId != 0)
    {
        scheduler.cancel(rtr.lsus().pacingTimerId);
        rtr.lsus().pacingTimerId = 0;
    }
}
} // namespace routing
