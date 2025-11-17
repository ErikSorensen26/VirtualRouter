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

bool ReliableTransport::setupReliablePacket(Neighbor* neighbor, EigrpHeader& builder)
{
    uint32_t seqNum = builder.getSequence();
    if (seqNum == 0)
        return false;

    // Create ReliablePacketInfo
    if (neighbor)
    {
        std::_Rb_tree_iterator<std::pair<const uint32_t, UnicastReliablePacket>> it;
        {
            std::lock_guard<std::mutex> lock(neighbor->reliableMtx);
            it = neighbor->reliableQueue.emplace(seqNum, UnicastReliablePacket{builder, neighbor->ipAddress}).first;
        }

        if (neighbor->currentReliable.load(std::memory_order_relaxed) == 0)
        {
            startUnicastReliable(*neighbor, it->second, seqNum);
            return true;
        }
    }
    else
    {
        std::unordered_map<Neighbor*, ReliableInfo> reliableMap;
        std::unordered_set<IPAddress> neighbors;

        std::shared_lock<std::shared_mutex> lock(ntable->neighborMutex);
        std::lock_guard<std::mutex> relock(reliableMtx);

        std::for_each(ntable->neighbors.begin(), ntable->neighbors.end(),
                      [&](auto& n) { reliableMap.emplace(n.second, ReliableInfo{}); neighbors.insert(n.first); });
        auto it = reliableQueue.emplace(seqNum, std::pair<std::unordered_set<IPAddress>, MulticastReliablePacket>{neighbors, {builder, reliableMap}});
        if (currentReliable.load(std::memory_order_relaxed) == 0)
        {
            startMulticastReliable(it.first->second.second, seqNum);
            return true;
        }
    }
    return false;
}

void ReliableTransport::startMulticastReliable(MulticastReliablePacket& pkt, uint32_t seq)
{
    auto& timer = iface.getTimers();
    for (auto& [nbr, info] : pkt.neighbors)
    {
        info.sendTime = std::chrono::steady_clock::now();
        info.retransmissionCount = 0;
        timer.startRetransmissionTimer(nbr, pkt, info, seq);
    }
    currentReliable.store(seq, std::memory_order_release);
}

void ReliableTransport::startUnicastReliable(Neighbor& neighbor, UnicastReliablePacket& pkt, uint32_t seq)
{
    pkt.info.sendTime = std::chrono::steady_clock::now();
    pkt.info.retransmissionCount = 0;
    iface.getTimers().startRetransmissionTimer(&neighbor, pkt, seq);
    neighbor.currentReliable.store(seq, std::memory_order_release);
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

    // Resend the packet
    PacketBuilder retransmissionPacket(iface.getIface());
    {
        // Construct and send the retransmission packet
        af == AddressFamily::IPv4
            ? Protocol::IPPacket::reserveIpv4(iface.getIface(), retransmissionPacket)
            : Protocol::IPPacket::reserveIpv6(iface.getIface(), retransmissionPacket);
        EigrpHeader eigrp;
        RESTORE_FULL_HEADER(retransmissionPacket, pkt.packet, eigrp, HeaderType::EIGRP);
        eigrp.setFlagCondRecv(false);

        auto* interface = iface.getIface();
        Protocol::IPPacket::BuildIP build = {
            .iface = interface,
            .packetInfo = retransmissionPacket,
            .destIp = neighbor->ipAddress.raw,
            .DSCP = iface.configs->DSCP.load(std::memory_order_relaxed),
            .protocolType = Variable::IP::eigrp
        };

        af == AddressFamily::IPv4
            ? Protocol::IPPacket::buildIpv4(build)
            : Protocol::IPPacket::buildIpv6(build);
    }

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

    // Resend the packet
    PacketBuilder retransmissionPacket(iface.getIface());
    {
        // Construct and send the retransmission packet
        af == AddressFamily::IPv4
            ? Protocol::IPPacket::reserveIpv4(iface.getIface(), retransmissionPacket)
            : Protocol::IPPacket::reserveIpv6(iface.getIface(), retransmissionPacket);
        EigrpHeader eigrp;
        RESTORE_FULL_HEADER(retransmissionPacket, pkt.packet, eigrp, HeaderType::EIGRP);

        auto* interface = iface.getIface();
        Protocol::IPPacket::BuildIP build = {
            .iface = interface,
            .packetInfo = retransmissionPacket,
            .destIp = pkt.destination.raw,
            .DSCP = iface.configs->DSCP.load(std::memory_order_relaxed),
            .protocolType = Variable::IP::eigrp
        };

        af == AddressFamily::IPv4
            ? Protocol::IPPacket::buildIpv4(build)
            : Protocol::IPPacket::buildIpv6(build);
    }

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
