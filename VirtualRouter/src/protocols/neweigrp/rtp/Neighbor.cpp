// Neighbor.cpp

#include "Neighbor.h"
#include <InterfaceTimers.h>
#include <EigrpInterface.h>
#include <EigrpCore.h>


namespace Eigrp
{
inline TLVType getTLVType(EigrpInterface& iface, Neighbor::Version v)
{
    Eigrp& base = iface.getBase();
    bool isV6 = base.getAF() == AddressFamily::IPv6;
    if (v == Neighbor::Version::LEGACY)
    {
        return isV6
            ? TLVType::LEGACY_V6
            : TLVType::LEGACY_V4;
    }
    else if (base.isNamed())
    {
        return isV6
            ? TLVType::WIDE_V6
            : TLVType::WIDE_V4;
    }
    else
    {
        return isV6
            ? TLVType::WIDE_V6
            : TLVType::LEGACY_V4;
    }
}

Neighbor::Neighbor(EigrpInterface& iface, InterfaceTimers& tmgr, const IPAddress& ip, Version v, bool unicast)
    : ipAddress(ip), unicast(unicast), version(v), tlvType(getTLVType(iface, v)), iface(iface), tmgr(tmgr)
{
    clear();
    if (!unicast)
        iface.tlvTypes[tlvType].insert(ip);
}

Neighbor::~Neighbor()
{
    if (!unicast)
        iface.tlvTypes[tlvType].erase(ipAddress);
    if (iface.tlvTypes[tlvType].empty())
        iface.tlvTypes.erase(tlvType);
}

void Neighbor::clear()
{
    state.store(State::DOWN);

    initSeq.store(0);

    holdTimerId.store(0);
    stuckInitTimerId.store(0);
    gracefulTimerId.store(0);

    holdTime.store(0);
    stuckInitActive.store(false);
    isGraceful.store(false);
    secondHello.store(false);

    initComplete.store(false);
    processAcks.store(false);
    hasMac.store(false);

    lastSeqRecv.store(0);
    lastHeard = std::chrono::steady_clock::time_point{};

    clearReliable();
}

void Neighbor::markHeard()
{
    lastHeard = std::chrono::steady_clock::now();
}

bool Neighbor::isActive() const noexcept
{
    return state.load() >= State::TWOWAY;
}

void Neighbor::clearReliable()
{
    {
        currentReliable.store(0, std::memory_order_release);
        uint32_t current = currentReliable.load(std::memory_order_relaxed);
        if (current != 0)
            iface.getTimers().cancelRetransmissionTimer(reliableQueue[current].info);
        std::lock_guard<std::mutex> lock(reliableMtx);
        reliableQueue.clear();
    }
    {
        auto& rtp = iface.getRtp();
        uint32_t current = rtp.currentReliable.load(std::memory_order_relaxed);
        if (current != 0)
        {
            auto& rel = rtp.reliableQueue[current];
            if (auto it = rel.second.neighbors.find(this); it != rel.second.neighbors.end())
            {
                iface.getTimers().cancelRetransmissionTimer(it->second);
            }
        }
        for (auto& reliable : rtp.reliableQueue)
        {
            if (auto it = reliable.second.second.neighbors.find(this); it != reliable.second.second.neighbors.end())
                reliable.second.second.neighbors.erase(it);
        }
    }
}
}
