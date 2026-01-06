// OspfNeighbor.cpp

#include "OspfNeighbor.h"
#include <LsaKey.hpp>
#include <OspfInterface.h>
#include <OspfInterfaceTimers.h>
#include <random>

static uint32_t generateInitialDDSequence()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    return dis(gen);
}

namespace OSPF
{
Neighbor::Neighbor(OspfInterface& iface, InterfaceTimers& tmgr, uint32_t rid, IPAddress& neighborIp, bool unicast)
    : ipAddress(neighborIp),
      unicast(unicast),
      routerID(rid),
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
            
            auto ntype = iface.configs->networkType.load(std::memory_order_relaxed);
            if (ntype == InterfaceConfigs::NetworkType::BROADCAST ||
                ntype == InterfaceConfigs::NetworkType::NON_BROADCAST)
            {
                if (!isBdr.load(std::memory_order_relaxed) &&
                    !isDr.load(std::memory_order_relaxed))
                    break;
            }
            setState(Neighbor::State::EXSTART);
            break;
        }

        case State::EXSTART:
        {
            if (oldState != State::EXSTART)
            {
                requestDbd.clear();
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
                if (requestDbd.empty())
                {
                    // Go straight to LOADING
                    setState(Neighbor::State::LOADING);
                }
                else
                {
                    state.store(s, std::memory_order_release);
                    iface.getDispatcher().sendReliableLSRequest(*this, requestDbd);
                }
            }
            break;
        }

        case State::FULL:
        {
            if (oldState == State::EXCHANGE || oldState == State::LOADING)
            {
                state.store(s, std::memory_order_release);
            }
            break;
        }
    }

    return getState() != oldState;
}
}
