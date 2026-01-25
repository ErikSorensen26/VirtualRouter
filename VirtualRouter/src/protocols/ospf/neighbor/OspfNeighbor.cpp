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
        case State::ATTEMPT:
        case State::INIT:
            state.store(s, std::memory_order_release);
            break;

        case State::TWOWAY:
        {
            state.store(s, std::memory_order_release);
            
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
                rtr.clearLsr();
                state.store(s, std::memory_order_release);
                iface.getDispatcher().sendInitDBD(*this);
            }
            break;
        }

        case State::EXCHANGE:
        {
            if (oldState == State::EXSTART)
            {
                state.store(s, std::memory_order_release);
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
                std::lock_guard<std::mutex> lock(rtr.getRelMtx());
                if (!rtr.getLsrActive())
                {
                    // Go straight to LOADING
                    setState(Neighbor::State::LOADING);
                }
                else
                {
                    state.store(s, std::memory_order_release);
                    iface.getDispatcher().sendReliableLSRequest(*this, rtr.getLsr());
                }
            }
            break;
        }

        case State::FULL:
        {
            if (oldState == State::EXCHANGE || oldState == State::LOADING)
            {
                state.store(s, std::memory_order_release);
                if (iface.demandCircuit.load(std::memory_order_relaxed) == OspfInterface::DcDecision::ENABLED)
                    iface.getTimers().stopHello();
            }
            break;
        }
    }

    return getState() != oldState;
}
}
