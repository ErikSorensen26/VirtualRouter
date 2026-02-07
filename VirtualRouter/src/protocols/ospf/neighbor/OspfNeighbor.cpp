// OspfNeighbor.cpp

#include "OspfNeighbor.h"
#include <LsaKey.hpp>
#include <OspfInterface.h>
#include <OspfInterfaceTimers.h>
#include <Interface.h>
#include <random>

static uint32_t generateInitialDDSequence()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    return dis(gen);
}

static uint16_t getMtu(bool isV6, OSPF::OspfInterface& iface)
{
    if (isV6)
        return iface.getIface().configs.ipv6.mtu.load(std::memory_order_relaxed);
    else
        return iface.getIface().configs.ipv4.mtu.load(std::memory_order_relaxed);
}

namespace OSPF
{
Neighbor::Neighbor(OspfInterface& iface, InterfaceTimers& tmgr, uint32_t rid, IPAddress& neighborIp, bool unicast)
    : ipAddress(neighborIp),
      unicast(unicast),
      routerID(rid),
      mtu(getMtu(neighborIp.isV6, iface)),
      currentSeq(generateInitialDDSequence()),
      rtr(iface.getProcess(), iface),
      iface(iface),
      tmgr(tmgr)
{}

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
            // TODO: clear routes out of lsdb
        }
        case State::ATTEMPT:
        case State::INIT:
            state = s;
            break;

        case State::TWOWAY:
        {
            state = s;
            
            auto ntype = iface.getConfigs().get<Config::OspfInterface::NETWORK>().load();
            if (ntype == NetworkType::BROADCAST ||
                ntype == NetworkType::NON_BROADCAST)
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
                if (iface.demandCircuit.load(std::memory_order_relaxed) == OspfInterface::DcDecision::ENABLED)
                    iface.getTimers().stopHello();
            }
            break;
        }
    }

    return getState() != oldState;
}
}
