// Neighbor.cpp

#include "Neighbor.h"
#include "eigrp/interface/InterfaceTimers.h"
#include "eigrp/interface/EigrpInterface.h"
#include "eigrp/core/Eigrp.h"

namespace routing::eigrp
{
inline TLVType getTLVType(EigrpInterface& iface, Neighbor::Version v)
{
    Eigrp& base = iface.getBase();
    bool isV6 = base.getAF() == types::AddressFamily::IPv6;
    if (v == Neighbor::Version::LEGACY)
    {
        return isV6
            ? TLVType::LEGACY_V6
            : TLVType::LEGACY_V4;
    }
    else if (base.isNamed())
    {
        return TLVType::WIDE;
    }
    else
    {
        return isV6
            ? TLVType::WIDE
            : TLVType::LEGACY_V4;
    }
}

Neighbor::Neighbor(EigrpInterface& iface, InterfaceTimers& tmgr, const types::IPAddress& ip, Version v, bool unicast)
    : ipAddress(ip), unicast(unicast), version(v), tlvType(getTLVType(iface, v)), iface(iface), tmgr(tmgr)
{
    clear();
    if (!unicast)
        iface.tlvTypes[tlvType].insert(ip);
}

Neighbor::~Neighbor()
{
    setState(State::DOWN);
    if (!unicast)
        iface.tlvTypes[tlvType].erase(ipAddress);
    if (iface.tlvTypes[tlvType].empty())
        iface.tlvTypes.erase(tlvType);

    iface.getTimers().cancelNeighborTimers(*this);
    iface.getBase().delGlobalNeighbor(ipAddress);
}

void Neighbor::clear()
{
    state.store(State::DOWN);

    recvInitSeq.store(0);
    sentInitSeq.store(0);

    holdTimerId.store(0);
    gracefulTimerId.store(0);

    holdTime.store(0);
    isGraceful.store(false);
    secondHello.store(false);

    initComplete.store(false);

    lastSeqRecv.store(0);

    clearReliable();
}

bool Neighbor::pushAck(uint32_t ack)
{
    if (outstandingAcks.contains(ack)) return false;
    ackQueue.push_back(ack);
    outstandingAcks.insert(ack);
    return true;
}

bool Neighbor::popAck(uint32_t& ack)
{
    if (ackQueue.empty()) return false;
    ack = ackQueue.front();
    ackQueue.pop_front();
    outstandingAcks.erase(ack);
    return true;
}

void Neighbor::removeAck(uint32_t ack)
{
    std::erase(ackQueue, ack);
    outstandingAcks.erase(ack);
}

bool Neighbor::hasAck(uint32_t ack)
{
    return outstandingAcks.contains(ack);
}

bool Neighbor::isActive() const noexcept
{
    return state.load() >= State::UP;
}

void Neighbor::clearReliable()
{
    auto& rtp = iface.getRtp();
    uint32_t current = currentReliable.load(std::memory_order_relaxed);
    currentReliable.store(0, std::memory_order_release);
    if (current != 0)
    {
        if (reliablePackets.count(current))
        {
            iface.getTimers().cancelRetransmissionTimer(reliablePackets[current].info);
        }
        else if (auto it = rtp.reliablePackets.find(current); it != rtp.reliablePackets.end())
        {
            it->second.neighbors.erase(this);
            if (it->second.neighbors.empty())
                rtp.reliablePackets.erase(current);
        }
    }
    if (!activeConditions.empty())
    {
        for (const auto& condition : activeConditions)
        {
            auto it = rtp.reliablePackets.find(condition);
            if (it == rtp.reliablePackets.end()) continue;
            it->second.neighbors.erase(this);
            if (it->second.neighbors.empty())
                rtp.reliablePackets.erase(it);
        }
    }
    reliableQueue.clear();
    activeConditions.clear();
    receivedConditions.clear();
}
} // namespace routing
