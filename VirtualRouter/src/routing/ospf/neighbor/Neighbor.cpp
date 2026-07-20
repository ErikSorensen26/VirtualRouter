// OspfNeighbor.cpp

#include <random>

#include "Neighbor.h"
#include "ospf/database/LsaKey.hpp"
#include "ospf/interface/OspfInterfaceBase.h"
#include "ospf/interface/InterfaceTimers.h"
#include "ospf/transmission/PacketDispatcher.h"
#include "ospf/area/Area.h"
#include "interface/Interface.h"

namespace routing
{

static uint32_t generateInitialDDSequence()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    return dis(gen);
}

static uint16_t getMtu(bool isV6, ospf::OspfInterfaceBase& iface)
{
    if (iface.isVirtualLink()) return 0;

    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return 0;
    return isV6 ? txIface->configs.ipv6.mtu.load(std::memory_order_relaxed)
                : txIface->configs.ipv4.mtu.load(std::memory_order_relaxed);
}

namespace ospf
{
Neighbor::Neighbor(OspfInterfaceBase& iface, InterfaceTimers& tmgr, uint32_t rid, types::IPAddress& neighborIp, bool unicast)
    : ipAddress(neighborIp),
      unicast(unicast),
      routerID(rid),
      mtu(getMtu(neighborIp.isIPv6(), iface)),
      currentSeq(generateInitialDDSequence()),
      rtr(iface, iface.getProcessConfigs()),
      iface(iface),
      tmgr(tmgr)
{}

Neighbor::~Neighbor()
{
    tmgr.cancleInactiveTimer(*this);
    tmgr.cancelRetransmissionTimers(*this);
}

void Neighbor::resetDbExchange()
{
    currentSeq = generateInitialDDSequence();
    currentDbd = std::nullopt;
}

bool Neighbor::setState(Neighbor::State s)
{
    State oldState = getState();

    switch (s)
    {
        case State::DOWN:
        {
            // Cancel and clear retransmission state
            tmgr.cancelRetransmissionTimers(*this);
            rtr.lsus().clear();
            rtr.lsrs().clear();

            // Flush all LSAs originated by this neighbor from the area LSDB
            iface.flushNeighborLsas(*this);
        }
        case State::ATTEMPT:
        case State::INIT:
            state = s;
            break;

        case State::TWOWAY:
        {
            state = s;
            
            auto ntype = iface.getNetworkType();
            if (ntype == config::ospf::NetworkType::BROADCAST ||
                ntype == config::ospf::NetworkType::NON_BROADCAST)
            {
                if (!isDr() && !isBdr())
                    break;
            }
            setState(Neighbor::State::EXSTART);
            break;
        }

        case State::EXSTART:
        {
            if (oldState != State::EXSTART)
            {
                tmgr.cancelLsrTimers(*this);
                rtr.lsrs().clear();
                state = s;
                iface.dispatcher.sendInitDbd(*this);
            }
            break;
        }

        case State::EXCHANGE:
        {
            if (oldState == State::EXSTART)
            {
                state = s;
                currentDbd = LsaKey{}; // Reset current LSA key
                if (getRole() == Role::MASTER)
                    iface.dispatcher.sendDbd(*this);
            }
            break;
        }

        case State::LOADING:
        {
            if (oldState == State::EXCHANGE)
            {
                state = s;
                if (!rtr.lsrs().getActive())
                {
                    // Nothing to request; go straight to FULL.
                    setState(Neighbor::State::FULL);
                }
                else
                {
                    iface.dispatcher.sendReliableLsr(*this, rtr.lsrs().getAll());
                }
            }
            break;
        }

        case State::FULL:
        {
            if (oldState == State::EXCHANGE || oldState == State::LOADING)
            {
                state = s;
                iface.setFloodReduction();
                if (iface.demandCircuit == OspfInterfaceBase::DcDecision::ENABLED)
                    tmgr.stopHello();
            }
            break;
        }
    }

    return getState() != oldState;
}
}

} // namespace routing
