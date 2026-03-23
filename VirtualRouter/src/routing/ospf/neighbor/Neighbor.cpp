// OspfNeighbor.cpp

#include <random>

#include "Neighbor.h"
#include "ospf/database/LsaKey.hpp"
#include "ospf/interface/OspfInterface.h"
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

static uint16_t getMtu(bool isV6, ospf::OspfInterface& iface)
{
    if (isV6)
        return iface.getIface().configs.ipv6.mtu.load(std::memory_order_relaxed);
    else
        return iface.getIface().configs.ipv4.mtu.load(std::memory_order_relaxed);
}

namespace ospf
{
Neighbor::Neighbor(OspfInterface& iface, InterfaceTimers& tmgr, uint32_t rid, types::IPAddress& neighborIp, bool unicast)
    : ipAddress(neighborIp),
      unicast(unicast),
      routerID(rid),
      mtu(getMtu(neighborIp.isIPv6(), iface)),
      currentSeq(generateInitialDDSequence()),
      rtr(iface.getProcess(), iface),
      iface(iface),
      tmgr(tmgr)
{}

Neighbor::~Neighbor()
{
    tmgr.cancleInactiveTimer(*this);
}

void Neighbor::resetDbExchange()
{
    currentSeq = generateInitialDDSequence();
    currentDbd = LsaKey{};
}

bool Neighbor::setState(Neighbor::State s)
{
    State oldState = getState();

    switch (s)
    {
        case State::DOWN:
        {
            // Clear retransmission lists
            rtr.lsus().clear();
            rtr.lsrs().clear();

            // Flush all LSAs originated by this neighbor from the area LSDB
            iface.getArea().flushNeighborLsas(routerID);
        }
        case State::ATTEMPT:
        case State::INIT:
            state = s;
            break;

        case State::TWOWAY:
        {
            state = s;
            
            auto ntype = iface.getConfigs().get<config::OspfInterface::NETWORK>().load();
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
                rtr.lsrs().clear();
                state = s;
                iface.getDispatcher().sendInitDBD(*this);
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
                    iface.getDispatcher().sendDBD(*this);
            }
            break;
        }

        case State::LOADING:
        {
            if (oldState == State::EXCHANGE)
            {
                if (!rtr.lsrs().getActive())
                {
                    // Go straight to LOADING
                    setState(Neighbor::State::LOADING);
                }
                else
                {
                    state = s;
                    iface.getDispatcher().sendReliableLSRequest(*this, rtr.lsrs().getAll());
                }
            }
            break;
        }

        case State::FULL:
        {
            if (oldState == State::EXCHANGE || oldState == State::LOADING)
            {
                state = s;
                if (iface.demandCircuit == OspfInterface::DcDecision::ENABLED)
                    iface.getTimers().stopHello();
            }
            break;
        }
    }

    return getState() != oldState;
}
}

} // namespace routing
