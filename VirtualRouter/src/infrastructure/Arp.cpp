// Arp.cpp

#include <Global.h>
#include <VirtualRouter.h>
#include "configs/registry/global/GlobalRegistry.h"
#include <ByteUtils.hpp>

#include "Arp.h"
#include "interface/Interface.h"
#include "packet/headers/ArpHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "Ethernet.h"
#include "configs/FieldAccessor.hpp"

// TODO: Have the ability to insert entries when shutdown

namespace infrastructure
{
// Constructor: Initiates the ARP object with the given interface
Arp::Arp(interface::Interface& interface) 
    : iface(interface),
      configs([&interface]() -> config::ArpRegistry& {
          return interface.configs.getConfigs().get<config::Interface::ARP>().get();
      }()),
      global(interface.getVRF()->getGlobal()),
      scheduler(interface.getScheduler().ref())
{
#ifdef DEBUG
    if (interface.getVRF()->getGlobal().routingEnabled)
        initiateArp();
#else
    initiateArp();
#endif
}

void Arp::refresh()
{
    scheduler.post([&] {
        clear();
        initiateArp();
    });
}

void Arp::initiateArp()
{
    iface.getVRF()->getConfigs().get<config::Vrf::ARP_STATIC_ENTRY>().withRead(
        [&](const auto& entries) {
            interface::InterfaceKey localKey = iface.configs.key;
            for (const auto& entry : entries)
            {
                const auto& key = config::StaticArpEntry::key(entry);
                if (localKey == key.value_or(localKey))
                    addStaticArpEntry(config::StaticArpEntry::address(entry),
                                      config::StaticArpEntry::mac(entry));
            }
        }
    );
    running.store(true, std::memory_order_release);
}

// Destructor
Arp::~Arp()
{
    scheduler.release();
    clear();
}

// Shutdown method
void Arp::shutdown()
{
    running.store(false, std::memory_order_release);
    scheduler.post([&]{ clear(); });
}

void Arp::clear()
{
    for (auto& [_, cache] : arpCache)
    {
        if (cache.expireTimerId)
            scheduler.cancel(cache.expireTimerId);
        if (cache.requestTimerId)
            scheduler.cancel(cache.requestTimerId);
    }

    // Clear resources
    arpCache.clear();
    arpTable.clear();
    incompletes = 0;
}

bool Arp::isShutdown()
{
    return !running.load(std::memory_order_relaxed);
}

void Arp::addArpEntry(types::IPv4Address targetIp, types::Mac targetMac, bool proxy)
{
    if (proxy)
    {
        proxyTable[targetIp] = targetMac;
    }
    else
    {
        ArpCacheEntry entry;
        entry.macAddress = targetMac;
        entry.status = ArpCacheStatus::COMPLETE;
        arpCache[targetIp] = std::move(entry);

        // Add to table
        arpTable.insert(targetIp, targetMac);
    }
}

void Arp::addStaticArpEntry(types::IPv4Address targetIp, types::Mac targetMac, bool proxy)
{
    scheduler.post([this, targetIp, targetMac, proxy]{
        addArpEntry(targetIp, targetMac, proxy);
    });
}

void Arp::removeStaticArpEntry(types::IPv4Address targetIp, bool proxy)
{
    scheduler.post([this, targetIp, proxy]{
        removeArpEntry(targetIp, proxy);
    });
}

void Arp::completeArpEntry(std::pair<const types::IPv4Address, ArpCacheEntry>& entry, types::Mac targetMac)
{
    auto& cache = entry.second;

    auto now = std::chrono::steady_clock::now();

    if (cache.status == ArpCacheStatus::COMPLETE && global.getConfigs().get<config::Global::IP_STICKY_ARP>().load())
        return;

    // Cancel old timer if already present
    if (cache.expireTimerId) scheduler.cancel(cache.expireTimerId);
    if (cache.requestTimerId) scheduler.cancel(cache.requestTimerId);

    if (cache.status == ArpCacheStatus::INCOMPLETE && incompletes != 0)
        --incompletes;

    // Build the entry
    cache.status = ArpCacheStatus::COMPLETE;
    cache.macAddress = targetMac;
    cache.retries = 0;
    cache.expiryTime = now + std::chrono::seconds(configs.get<config::Arp::TIMEOUT>().load());

    // Update the data-plane table
    arpTable.insert(entry.first, targetMac);
    cache.renewalTime = now + std::chrono::seconds((configs.get<config::Arp::TIMEOUT>().load() * 10) / 8); // Renewal is 80 percent

    // Refresh vs expire logic
    cache.requestTimerId = scheduler.postAfter(
        cache.renewalTime,
        [this, ip = entry.first](uint32_t) { renewArpEntry(ip); }
    );

    cache.expireTimerId = scheduler.postAfter(
        cache.expiryTime,
        [this, ip = entry.first](uint32_t) { removeArpEntry(ip); }
    );

    processQueuedPackets(entry.first, targetMac);
}

void Arp::renewArpEntry(types::IPv4Address ip)
{
    if (auto it = arpCache.find(ip); it != arpCache.end())
    {
        it->second.requestTimerId = 0;
        sendRequest(ip, it->second);
    }
}

void Arp::removeArpEntry(types::IPv4Address ip, bool proxy)
{
    if (proxy)
    {
        proxyTable.erase(ip);
    }
    else
    {
        if (auto cacheIt = arpCache.find(ip); cacheIt != arpCache.end())
        {
            if (cacheIt->second.expireTimerId)
                scheduler.cancel(cacheIt->second.expireTimerId);
            if (cacheIt->second.requestTimerId)
                scheduler.cancel(cacheIt->second.requestTimerId);
            arpCache.erase(ip);
        }
        arpTable.erase(ip);
    }
}

// Get MAC address for the given ip
bool Arp::getMac(uint8_t* out, types::IPv4Address targetIp)
{
    return arpTable.findAndWrite<48>(targetIp, out);
}

// Enqueue a packet for ARP resolution and send once resolved
void Arp::resolveAndSend(types::IPv4Address targetIp, processing::PacketBuilder& packetToSend)
{
    if (!global.getConfigs().get<config::Global::IP_ARP_INCOMPLETE>().load())
        return;

    bool cached = arpCache.contains(targetIp);

    // Entry limit enforcement, only enforce if a new entry is required
    if (!cached)
    {
        auto incompleteEntries = global.getConfigs().get<config::Global::IP_ARP_INCOMPLETE_ENTRIES>();
        if (incompleteEntries.hasValue() && incompletes >= incompleteEntries.load())
            return; // Too many incomplete entries
    }

    // Make new entry if not already made
    ArpCacheEntry& cache = arpCache[targetIp];

    auto& queue = cache.queue;

    if (cache.status == ArpCacheStatus::COMPLETE)
    {
        sendQueuedPacket(packetToSend, cache.macAddress);
        return;
    } 

    // Enforce queue size limit from global config
    if (queue.size() < global.getConfigs().get<config::Global::IP_ARP_QUEUE>().load())
    {
        queue.push(std::move(packetToSend));
    }

    if (!cached) incompletes++;

    sendRequest(targetIp, cache);
}

// Send an ARP request for the given IP
void Arp::sendRequest(types::IPv4Address targetIp, ArpCacheEntry& cache)
{
    if (cache.requestTimerId) return; // Request already active
    scheduleRequest(targetIp, cache);
}

// Method to receive ARP reply
void Arp::receiveReply(const packet::ArpHeader& receivedReply)
{
    const uint8_t* mac = receivedReply.getSenderHwAddr();
    types::IPv4Address senderIp = receivedReply.getSenderIpAddr();
    types::IPv4Address targetIp = receivedReply.getTargetIpAddr();

    // Ignore gratuitous ARP if disabled
    bool garp = senderIp == targetIp;
    if (garp && (!global.getConfigs().get<config::Global::IP_ARP_GRATUITOUS>().load() ||
        !running.load(std::memory_order_relaxed) || !global.routingEnabled ||
        std::memcmp(mac, ETHERNET_MAC_BROADCAST, 6) == 0))
        return;

    if (auto cacheIt = arpCache.find(senderIp); cacheIt != arpCache.end())
    {
        if (arpCache[senderIp].status == ArpCacheStatus::COMPLETE &&
            global.getConfigs().get<config::Global::IP_STICKY_ARP>().load())
            return;

        completeArpEntry(*cacheIt, utils::read<uint64_t, 6>(mac));
    }
    else if (garp)
    {
        auto [it, ok] = arpCache.emplace(senderIp, ArpCacheEntry{});
        completeArpEntry(*it, utils::read<uint64_t, 6>(mac));
    }
    else return;
}

void Arp::receiveRequest(const packet::ArpHeader& request, types::Mac sourceMac)
{
    types::IPv4Address targetIp = request.raw->targetIpAddress;
    types::IPv4Address senderIp = request.raw->senderIpAddress;

    if (configs.get<config::Arp::AUTHORIZED>().load() && !arpCache.contains(senderIp))
        return;

    // Drop invalid request (e.g., 0.0.0.0 or identical source/target)
    if (targetIp.addr == 0 || targetIp == senderIp)
        return;

    bool isLocal = false;
    bool isProxy = false;

    if (iface.configs.ipv4.comparePrimaryAddress(request.raw->targetIpAddress))
    {
        isLocal = true;
    }
    else if (global.getConfigs().get<config::Global::IP_ARP_PROXY>().load())
    {
        if (auto proxyIt = proxyTable.find(request.raw->targetIpAddress); proxyIt != proxyTable.end())
        {
            isProxy = true; // Static proxy entry
        }
        else
        {
            utils::RCU::Guard g;
            core::RibEntry<uint32_t>* entry = iface.getVRF()->getRib().lookup(targetIp.addr, g);
            if (entry && !entry->hasNextHopInterface(iface.configs.key.getId()) && entry->prefix != 0)
            {
                isProxy = true;
            }
        }
    }

    if (!isLocal && !isProxy)
        return; // Not for us

    processing::PacketBuilder reply(&iface);
    arpReply(reply, sourceMac, request.getTargetIpAddr(), request.getSenderIpAddr());
}

void Arp::processQueuedPackets(types::IPv4Address targetIp, types::Mac macAddress)
{
    auto cacheIt = arpCache.find(targetIp);
    if (cacheIt == arpCache.end())
        return;

    auto& packets = cacheIt->second.queue;

    while (!packets.empty())
    {
        sendQueuedPacket(packets.front(), macAddress);
        packets.pop();
    }
}

void Arp::sendQueuedPacket(processing::PacketBuilder& pkt, types::Mac mac)
{
    auto current = pkt.previewNextBuildHeader();
    if (!current) return;

    // Continue building next header
    switch (current->type)
    {
        //TODO add more headers
        case packet::HeaderType::ETHERNET:
            ethernet::build(&iface, pkt, mac, ETHERNET_IPV4);
            break;
        default:
            return;
    }
}

void Arp::scheduleRequest(types::IPv4Address targetIp, ArpCacheEntry& entry)
{
    if (!running.load(std::memory_order_relaxed) || !global.routingEnabled) return;

    uint32_t interval;
    uint32_t maxRetries;
    if (entry.status == ArpCacheStatus::INCOMPLETE)
    {
        interval = 1; // 1-second retransmit interval for initial ARP resolution
        maxRetries = global.getConfigs().get<config::Global::IP_ARP_INCOMPLETE_RETRY>().load();
    }
    else
    {
        interval = configs.get<config::Arp::PROBE_INTERVAL>().load();
        maxRetries = configs.get<config::Arp::PROBE_COUNT>().load();
    }

    entry.retries++;

    if (entry.retries > maxRetries)
    {
        removeArpEntry(targetIp);
        return;
    }

    // Send the ARP request
    processing::PacketBuilder arpReq(&iface);
    arpRequest(arpReq, iface.configs.ipv4.getPrimaryAddress(), targetIp);

    // Schedule next retry
    entry.requestTimerId = scheduler.postAfter(
        std::chrono::steady_clock::now() + std::chrono::seconds(interval),
        [this, targetIp](uint32_t) {
            auto it = arpCache.find(targetIp);
            if (it != arpCache.end())
            {
                it->second.requestTimerId = 0;
                scheduleRequest(targetIp, it->second);
            }
        }
    );
}

// Method to send an ARP reply
void Arp::sendReply(types::Mac targetMac, types::IPv4Address targetIp)
{
    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder replyPacket(&iface);
    arpReply(replyPacket, targetMac, iface.configs.ipv4.getPrimaryAddress(), targetIp);
}

// Creates and sends an ARP request packet
void Arp::arpRequest(processing::PacketBuilder& packet, types::IPv4Address sourceIp, types::IPv4Address targetIp)
{
    ethernet::reserve(packet);
    packet.reserveHeader(packet::HeaderType::ARP, packet::ArpHeader::fixedSize);

    packet::ArpHeader arp;
    arp.setBuffer(packet.nextBuildHeader()->buffer);

    arp.setHardwareType(ARP_HARDWARE_ETHERNET);
    arp.setProtocolType(ETHERNET_IPV4);
    arp.setHardwareSize(0x06);
    arp.setProtocolSize(0x04);
    arp.setOpcode(ARP_OPCODE_REQUEST);
    arp.setSenderHwAddr(iface.configs.getMac());
    arp.setSenderIpAddr(sourceIp.addr);
    std::memcpy(arp.raw->targetHardwareAddress, ETHERNET_MAC_BROADCAST, 6);
    arp.setTargetIpAddr(targetIp.addr);

    ethernet::build(&iface, packet, utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), ETHERNET_ARP);
}

// Creates and sends an ARP reply packet
void Arp::arpReply(processing::PacketBuilder& packet, types::Mac targetMac, types::IPv4Address sourceIp, types::IPv4Address targetIp)
{
    ethernet::reserve(packet);
    packet.reserveHeader(packet::HeaderType::ARP, packet::ArpHeader::fixedSize);

    packet::ArpHeader arp;
    arp.setBuffer(packet.nextBuildHeader()->buffer);

    arp.setHardwareType(ARP_HARDWARE_ETHERNET);
    arp.setProtocolType(ETHERNET_IPV4);
    arp.setHardwareSize(0x06);
    arp.setProtocolSize(0x04);
    arp.setOpcode(ARP_OPCODE_REPLY);
    arp.setSenderHwAddr(iface.configs.getMac());
    arp.setSenderIpAddr(sourceIp.addr);
    arp.setTargetHwAddr(targetMac);
    arp.setTargetIpAddr(targetIp.addr);

    ethernet::build(&iface, packet, targetMac, ETHERNET_ARP);
}
} // namespace infrastructure
