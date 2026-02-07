// ReliableTransport.cpp

/*
 * implicit acks are always allowed
 *
 * Steps
 * 1. init updates are sent with topology tables
 * 2. explicit acks are not allowed until syncronization
 * 3. init update will be retransmitted from rtp
 * 4. update will implicitly ack the other init because its allowed to
 * 5. once the first init is acked, neighbors are syncronized
 */

#include "ReliableTransport.h"
#include "EigrpInterface.h"
#include "NeighborTable.h"
#include "AuthHandler.h"
#include "EigrpPacketBuilder.h"
#include <Eigrp.h>

#include <PacketBuilder.hpp>
#include <IPPacket.h>
#include <Ethernet.h>
#include <EigrpHeader.hpp>
#include <StaticHeader.hpp>

namespace Eigrp
{
ReliableTransport::ReliableTransport(EigrpInterface& iface) : iface(iface)
{
    ntable = &iface.getNTable();
    auto& base = iface.getBase();
    af = base.getAF();
    as = base.getAS();
}

ReliableTransport::~ReliableTransport()
{
    auto& tmgr = iface.getTimers();
    for (auto& [seq, pkt] : reliablePackets)
    {
        for (auto& [nbr, info] : pkt.neighbors)
            tmgr.cancelRetransmissionTimer(info);
    }
}

bool ReliableTransport::setupUnicastReliable(Neighbor& neighbor, EigrpHeader& builder)
{
    uint32_t seqNum = builder.getSequence();
    if (seqNum == 0)
        return false;

    uint32_t current = 0;
    std::_Rb_tree_iterator<std::pair<const uint32_t, UnicastReliablePacket>> it;
    {
        std::lock_guard<std::mutex> lock(neighbor.reliableMtx);
        it = neighbor.reliablePackets.emplace(seqNum, UnicastReliablePacket{builder, neighbor.ipAddress}).first;
        it->second.info.sequence = seqNum;
        current = neighbor.currentReliable.load(std::memory_order_relaxed);
        if (current != 0)
        {
            neighbor.reliableQueue.push_back({seqNum, false});
            return false;
        }
    }

    neighbor.currentReliable.store(seqNum, std::memory_order_release);
    startUnicastReliable(neighbor, it->second, seqNum);
    return true;
}

bool ReliableTransport::setupMulticastReliable(EigrpHeader& builder)
{
    uint32_t seqNum = builder.getSequence();
    if (seqNum == 0)
        return false;

    std::vector<IPAddress> conditions;
    std::shared_lock<std::shared_mutex> lock(ntable->neighborMutex);
    for (auto& [ip, nbr] : ntable->neighbors)
    {
        if (nbr.currentReliable.load(std::memory_order_relaxed) != 0)
        {
            conditions.push_back(nbr.ipAddress);
            std::lock_guard<std::mutex> relock(nbr.reliableMtx);

            nbr.activeConditions.insert(seqNum);
            nbr.reliableQueue.push_back({seqNum, true});
        }
        else
        {
            nbr.currentReliable.store(seqNum, std::memory_order_release);
        }
    }

    std::unordered_map<Neighbor*, ReliableInfo> reliableMap;
    std::lock_guard<std::mutex> relock(reliableMtx);

    for (auto& [ip, nbr] : ntable->neighbors)
    {
        auto it = reliableMap.emplace(&nbr, ReliableInfo{});
        it.first->second.sequence = seqNum;
    }

    auto it = reliablePackets.emplace(seqNum, MulticastReliablePacket{builder, reliableMap});

    if (!conditions.empty())
    {
        sendConditionalHello(conditions, seqNum);
        builder.setFlagCondRecv(true);
    }

    startMulticastReliable(it.first->second, seqNum);
    return true;
}

void ReliableTransport::startMulticastReliable(MulticastReliablePacket& pkt, uint32_t seq)
{
    auto& timer = iface.getTimers();
    for (auto& [nbr, info] : pkt.neighbors)
    {
        if (nbr->currentReliable.load(std::memory_order_relaxed) == seq)
        {
            info.sendTime = std::chrono::steady_clock::now();
            info.retransmissionCount = 0;
            timer.startRetransmissionTimer(nbr, pkt, info, seq);
        }
    }
}

void ReliableTransport::startUnicastReliable(Neighbor& neighbor, UnicastReliablePacket& pkt, uint32_t seq)
{
    pkt.info.sendTime = std::chrono::steady_clock::now();
    pkt.info.retransmissionCount = 0;
    iface.getTimers().startRetransmissionTimer(&neighbor, pkt, seq);
}

void ReliableTransport::sendRetransmission(Neighbor& neighbor, StaticHeader& header)
{
    // Construct and send the retransmission packet
    PacketBuilder retransmissionPacket(iface.getIface());
    af == AddressFamily::IPv4
        ? Protocol::IPPacket::reserveIpv4(retransmissionPacket)
        : Protocol::IPPacket::reserveIpv6(retransmissionPacket);
    auto* hdr = retransmissionPacket.addHeader(header, HeaderType::EIGRP);
    EigrpHeader eigrp;
    eigrp.setBuffer(hdr->buffer);
    eigrp.setFlagCondRecv(false);

    auto* interface = iface.getIface();
    Protocol::IPPacket::BuildIP build = {
        .iface = interface,
        .packetInfo = retransmissionPacket,
        .destIp = neighbor.ipAddress.raw,
        .DSCP = iface.configs->DSCP.load(std::memory_order_relaxed),
        .protocolType = IP_EIGRP
    };

    af == AddressFamily::IPv4
        ? Protocol::IPPacket::buildIpv4(build)
        : Protocol::IPPacket::buildIpv6(build);
}

void ReliableTransport::handleRetransmission(Neighbor* neighbor, MulticastReliablePacket& pkt, ReliableInfo& info, uint32_t seq)
{
    // Retrieve the packet information under a shared lock
    auto it = pkt.neighbors.find(neighbor);
    if (it == pkt.neighbors.end()) return;
    auto& pktInfo = it->second;

    // Handle retransmission limit
    if (pktInfo.retransmissionCount >= MAX_RETRANSMISSIONS)
    {
        iface.getTimers().cancelRetransmissionTimer(pktInfo);
        ntable->onDown(*neighbor);
        return;
    }

    if (!pkt.packet.buffer)
    {
        iface.getTimers().cancelRetransmissionTimer(pktInfo);
        return;
    }

    // Resend the packet
    sendRetransmission(*neighbor, pkt.packet);

    // Increment retransmission timer safely
    double newRto = neighbor->srtt.load(std::memory_order_relaxed) + std::max(0.1, 4.0 * neighbor->rttvar.load(std::memory_order_relaxed));
    neighbor->rto.store(std::clamp(newRto, 1.0, 60.0), std::memory_order_release);
    pktInfo.retransmissionCount++;
    pktInfo.sendTime = std::chrono::steady_clock::now();
    iface.getTimers().startRetransmissionTimer(neighbor, pkt, info, seq);
}

void ReliableTransport::handleRetransmission(Neighbor* neighbor, UnicastReliablePacket& pkt, uint32_t seq)
{
    // Handle retransmission limit
    if (pkt.info.retransmissionCount >= MAX_RETRANSMISSIONS)
    {
        iface.getTimers().cancelRetransmissionTimer(pkt.info);
        ntable->onDown(*neighbor);
        return;
    }
    
    if (!pkt.packet.buffer)
    {
        iface.getTimers().cancelRetransmissionTimer(pkt.info);
        return;
    }

    // Resend the packet
    sendRetransmission(*neighbor, pkt.packet);

    // Increment retransmission timer safely
    neighbor->rto = std::min(neighbor->rto * 2.0, 60.0);
    pkt.info.retransmissionCount++;
    pkt.info.sendTime = std::chrono::steady_clock::now();
    iface.getTimers().startRetransmissionTimer(neighbor, pkt, seq);
}

uint16_t ReliableTransport::getMtu()
{
    return af == AddressFamily::IPv4
        ? iface.getIface()->configs.ipv4.mtu.load(std::memory_order_relaxed)
        : iface.getIface()->configs.ipv6.mtu.load(std::memory_order_relaxed);
}

uint32_t ReliableTransport::incrementSequenceNumber() 
{
    uint32_t seq = nextSeq.fetch_add(1, std::memory_order_release);
    if (seq == std::numeric_limits<uint32_t>::max())
        nextSeq.store(1, std::memory_order_relaxed);
    return seq;
}

void ReliableTransport::parseEigrpOptionHelper(const EigrpHeader& hdr, std::vector<TLV16Option>& options)
{
    auto trail = hdr.getTrail();
    parseEigrpOptions(trail.data(), trail.size(), options);
}

bool ReliableTransport::verifyNeighborAS(const EigrpHeader& hdr)
{
    return hdr.getAutonomousSystem() == as;
}
}
