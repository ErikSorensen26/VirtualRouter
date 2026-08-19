// Ndp.cpp

// TODO: naglean  — learn neighbor from unsolicited NA when NA_GLEAN is enabled
// TODO: nudigp   — use NUD reachability for IGP next-hop tracking

#include <Global.h>
#include <VirtualRouter.h>
#include <ByteUtils.hpp>

#include "Ndp.h"
#include "core/routing/rib/RibEntry.hpp"
#include "IPPacket.h"
#include "Ethernet.h"
#include "processing/PacketBuilder.hpp"
#include "interface/Interface.h"
#include "configs/FieldAccessor.hpp"

namespace infrastructure
{

uint8_t* calculateEui64(uint8_t* out, const uint8_t* prefix, const uint8_t* mac)
{
    std::memcpy(out, prefix, 8);
    out[8]  = mac[0] ^ 0x02;
    out[9]  = mac[1];
    out[10] = mac[2];
    out[11] = 0xFF;
    out[12] = 0xFE;
    out[13] = mac[3];
    out[14] = mac[4];
    out[15] = mac[5];
    return out;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Ndp::Ndp(interface::Interface& interface)
    : iface(interface),
      configs([&interface]() -> config::NdpRegistry& {
          return interface.configs.getConfigs().get<config::Interface::IPV6_ND>().get();
      }()),
      global(interface.getVRF()->getGlobal()),
      scheduler(interface.getScheduler().ref())
{
#ifdef DEBUG
    if (global.routingEnabled)
        initiateNdp();
#else
    initiateNdp();
#endif
}

Ndp::~Ndp()
{
    scheduler.release();
    clear();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Ndp::refresh()
{
    scheduler.post([this] {
        clear();
        initiateNdp();
    });
}

void Ndp::initiateNdp()
{
    running.store(true, std::memory_order_release);
    if (!configs.get<config::Ndp::RA_SUPPRESS_ALL>().load())
        scheduleNextRA();
}

void Ndp::shutdown()
{
    running.store(false, std::memory_order_release);
    scheduler.post([this] { clear(); });
}

bool Ndp::isShutdown()
{
    return !running.load(std::memory_order_relaxed);
}

void Ndp::clear()
{
    // Cancel all neighbor timers
    for (auto& [ip, entry] : ndpCache)
    {
        if (entry.timerId)      scheduler.cancel(entry.timerId);
        if (entry.retryTimerId) scheduler.cancel(entry.retryTimerId);
    }
    // Cancel RA timers
    for (uint32_t timerId : raTimerIds)
        scheduler.cancel(timerId);

    // Cancel default router timers and withdraw installed default routes
    for (auto& [routerIp, timerId] : defaultRouterTimers)
    {
        scheduler.cancel(timerId);
        uint32_t pid = (uint32_t)std::hash<__uint128_t>{}(routerIp.addr);
        iface.getVRF()->getRib().removeRoute<__uint128_t>(0, 0, core::RouteSource::DYNAMIC, pid);
    }

    ndpCache.clear();
    ndpTable.clear();
    raTimerIds.clear();
    raReceivedTimestamps.clear();
    lastUnsolicitedNaTime.clear();
    defaultRouterTimers.clear();
    incompletes = 0;
}

// ---------------------------------------------------------------------------
// Fast-path MAC lookup
// ---------------------------------------------------------------------------

uint8_t* Ndp::getMac(uint8_t* out, types::IPv6Address ip)
{
    if (ndpTable.findAndWrite<48>(ip, out))
        return out;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void Ndp::completeNdpEntry(types::IPv6Address targetIp, NdpCacheEntry& entry, types::Mac mac)
{
    if (entry.timerId)      { scheduler.cancel(entry.timerId);      entry.timerId = 0; }
    if (entry.retryTimerId) { scheduler.cancel(entry.retryTimerId); entry.retryTimerId = 0; }

    if (entry.state == NudState::INCOMPLETE)
        incompletes--;
    if (entry.state == NudState::PROBE)
        nuds--;

    entry.macAddress = mac;
    entry.state      = NudState::REACHABLE;
    entry.retries    = 0;

    ndpTable.insert(targetIp, mac);

    auto refreshPeriod = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::NUD_REFRESH_PERIOD>();
    if (refreshPeriod.hasValue())
    {
        entry.timerId = scheduler.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(refreshPeriod.load()),
            [this, targetIp](uint32_t) { refreshNeighborEntry(targetIp); }
        );
    }
    else
    {
        uint32_t reachableTime = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().load();
        entry.timerId = scheduler.postAfter(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(reachableTime),
            [this, targetIp](uint32_t) { onReachableTimeout(targetIp); }
        );
    }

    sendQueuedPackets(targetIp, mac);
}

void Ndp::addNdpEntry(types::IPv6Address targetIp, types::Mac targetMac, bool proxy)
{
    if (proxy)
    {
        proxyTable[targetIp] = targetMac;
    }
    else
    {
        auto& entry = ndpCache[targetIp];
        entry.macAddress = targetMac;
        entry.state = NudState::REACHABLE;
        ndpCache[targetIp] = std::move(entry);

        // Add to table
        ndpTable.insert(targetIp, targetMac);
    }
}

void Ndp::addStaticNdpEntry(types::IPv6Address targetIp, types::Mac targetMac, bool proxy)
{
    scheduler.post([this, targetIp, targetMac, proxy]{
        addNdpEntry(targetIp, targetMac, proxy);
    });
}

void Ndp::removeStaticNdpEntry(types::IPv6Address targetIp, bool proxy)
{
    scheduler.post([this, targetIp, proxy] {
        removeNdpEntry(targetIp, proxy);
    });
}

void Ndp::removeNdpEntry(types::IPv6Address targetIp, bool proxy)
{
    if (proxy)
    {
        proxyTable.erase(targetIp);
    }
    else
    {
        auto it = ndpCache.find(targetIp);
        if (it == ndpCache.end()) return;

        auto& entry = it->second;
        if (entry.timerId)      scheduler.cancel(entry.timerId);
        if (entry.retryTimerId) scheduler.cancel(entry.retryTimerId);

        ndpCache.erase(it);
        ndpTable.erase(targetIp);
    }
}

// ---------------------------------------------------------------------------
// Packet queuing and resolution
// ---------------------------------------------------------------------------

void Ndp::resolveAndSend(types::IPv6Address targetIp, processing::PacketBuilder& packetToSend)
{
    bool cached = ndpCache.contains(targetIp);

    // Entry limit enforcement, only enforce if a new entry is required
    if (!cached)
    {
        auto incompleteEntries = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_INTERFACE_LIMIT>();
        if (incompleteEntries.hasValue() && incompletes >= incompleteEntries.load())
            return; // Too many incomplete entries
    }

    NdpCacheEntry& entry = ndpCache[targetIp];

    auto& queue = entry.queue;

    if (cached)
    {
        bool strict = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::HOST_MODE_STRICT>().load();

        if (entry.state == NudState::REACHABLE)
        {
            if (!strict)
                sendQueuedPacket(packetToSend, entry.macAddress);
            else
                startNud(targetIp, entry);
        }
        else if (entry.state == NudState::STALE)
        {
            startNud(targetIp, entry);
        }
        return;
    }

    // Queue the packet
    uint8_t queueLimit = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::RESOLUTION_DATA_LIMIT>().load();
    if (queue.size() < queueLimit)
        queue.push(std::move(packetToSend));

    if (!cached) incompletes++;

    sendNeighborSolicitation(targetIp, entry);
}

void Ndp::sendQueuedPackets(types::IPv6Address targetIp, types::Mac macAddress)
{
    auto cacheIt = ndpCache.find(targetIp);
    if (cacheIt == ndpCache.end())
        return;

    auto& packets = cacheIt->second.queue;

    while (!packets.empty())
    {
        sendQueuedPacket(packets.front(), macAddress);
        packets.pop();
    }
}

void Ndp::sendQueuedPacket(processing::PacketBuilder& pkt, types::Mac mac)
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

// ---------------------------------------------------------------------------
// Neighbor Solicitation / NUD
// ---------------------------------------------------------------------------

void Ndp::sendNeighborSolicitation(types::IPv6Address targetIp, NdpCacheEntry& entry)
{
    if (entry.retryTimerId) return; // Request already active
    scheduleNeighborSolicitation(targetIp, entry);
}

void Ndp::scheduleNeighborSolicitation(types::IPv6Address targetIp, NdpCacheEntry& entry)
{
    if (!running.load(std::memory_order_relaxed) || entry.state == NudState::REACHABLE)
        return;
    if (entry.state == NudState::STALE)
    {
        startNud(targetIp, entry);
        return;
    }

    uint8_t attempt    = entry.retries;
    bool runningNud    = false;
    uint32_t maxRetries;

    auto state = entry.state;
    if (state == NudState::PROBE)
    {
        runningNud = true;
        maxRetries = configs.get<config::Ndp::NUD_RETRY_ATTEMPTS>().load();
    }
    else
    {
        maxRetries = 3;
    }

    // On exhaustion
    if (attempt >= maxRetries)
    {
        if (runningNud)
        {
            // Transition to UNREACHABLE with final-wait timer
            uint16_t finalWait    = configs.get<config::Ndp::NUD_FINAL_WAIT>().load();
            entry.retryTimerId = scheduler.postAfter(
                std::chrono::steady_clock::now() + std::chrono::milliseconds(finalWait),
                [this, targetIp](uint32_t) { removeNdpEntry(targetIp); }
            );
        }
        else removeNdpEntry(targetIp);
        return;
    }

    // Send NS
    if (!iface.shutdownFlag.load(std::memory_order_relaxed))
    {
        uint64_t mac = iface.configs.getMac();
        processing::PacketBuilder nsPacket(&iface);
        neighborSolicitation(nsPacket, targetIp, &mac);

        ippacket::BuildIP build = {
            .iface       = &iface,
            .packetInfo  = nsPacket,
            .destIp      = generateMulticastSolicitationAddress(targetIp),
            .protocolType = IP_ICMPV6
        };
        ippacket::buildIpv6(build);
    }

    entry.retries++;

    uint32_t interval = runningNud
        ? configs.get<config::Ndp::NUD_RETRY_INTERVAL>().load()
        : configs.get<config::Ndp::NS_INTERVAL>().load();

    if (runningNud) // Apply nud multiplier (base)
    {
        uint16_t multiplier  = configs.get<config::Ndp::NUD_RETRY>().load();
        for (int i = 0; i < static_cast<int>(attempt); i++)
        {
            interval *= multiplier;
        }
    }

    if (entry.retryTimerId)
        scheduler.cancel(entry.retryTimerId);

    entry.retryTimerId = scheduler.postAfter(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(interval),
        [this, targetIp](uint32_t)
        {
            if (auto entryIt = ndpCache.find(targetIp); entryIt != ndpCache.end())
                scheduleNeighborSolicitation(targetIp, entryIt->second);
        }
    );
}

void Ndp::startNud(types::IPv6Address targetIp, NdpCacheEntry& entry)
{
    uint16_t nudLimit = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::NUD_LIMIT>().load();
    if (nudLimit != 0 && nuds >= nudLimit)
        return;

    entry.state   = NudState::PROBE;
    entry.retries = 0;
    nuds++;

    scheduleNeighborSolicitation(targetIp, entry);
}

void Ndp::onReachableTimeout(types::IPv6Address targetIp)
{
    auto it = ndpCache.find(targetIp);
    if (it == ndpCache.end()) return;

    it->second.timerId = 0;

    bool doRefresh = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_REFRESH>().load();
    if (doRefresh)
    {
        startNud(targetIp, it->second);
    }
    else
    {
        it->second.state = NudState::STALE;

        uint16_t cacheExpire = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_EXPIRE>().load();
        it->second.timerId = scheduler.postAfter(
            std::chrono::steady_clock::now() + std::chrono::seconds(cacheExpire),
            [this, targetIp](uint32_t) { removeNdpEntry(targetIp); }
        );
    }
}

void Ndp::refreshNeighborEntry(types::IPv6Address targetIp)
{
    auto it = ndpCache.find(targetIp);
    if (it == ndpCache.end()) return;

    it->second.timerId = 0;
    if (it->second.state == NudState::REACHABLE)
        startNud(targetIp, it->second);
}

// ---------------------------------------------------------------------------
// Receive handlers
// ---------------------------------------------------------------------------

void Ndp::receiveNeighborAdvertisement(const packet::Icmpv6Header& receivedNA, types::IPv6Address targetIp)
{
    auto trail   = receivedNA.getTrail();
    types::Mac mac;
    bool macFound = false;

    std::vector<packet::TLV8Option> options;
    packet::parseIcmpv6Options(trail.data() + 16, trail.size() - 16, options);

    for (const auto& opt : options)
    {
        if (opt.type == ICMPV6_OPTION_NDP_TARGET && opt.valueSize == 6)
        {
            mac      = utils::read<uint64_t, 6>(opt.value);
            macFound = true;
            break;
        }
    }
    if (!macFound) return;

    // Check if this NA is for a tentative DAD address
    {
        __uint128_t addrValue = utils::read<__uint128_t>(trail.data());
        for (const auto& addr : iface.configs.ipv6.globalAddresses)
        {
            if (addr->prefix == addrValue && addr->tentative)
            {
                dadEntries[addrValue].duplicate = true;
                return;
            }
        }
    }

    auto it = ndpCache.find(targetIp);
    if (it != ndpCache.end())
    {
        completeNdpEntry(targetIp, it->second, mac);
    }
    else
    {
        // Unsolicited NA (NA_GLEAN): learn from it if configured
        if (configs.get<config::Ndp::NA_GLEAN>().load())
        {
            addNdpEntry(targetIp, mac);
            completeNdpEntry(targetIp, ndpCache[targetIp], mac);
        }
    }
}

void Ndp::receiveNeighborSolicitation(const packet::Icmpv6Header& nsHeader, types::IPv6Address srcIp, types::Mac srcMac)
{
    auto trail = nsHeader.getTrail();
    types::IPv6Address targetIp = utils::read<__uint128_t>(trail.data());

    bool isOwned = false;
    bool isProxy = false;

    if (iface.configs.ipv6.hasAddress(trail.data()))
    {
        isOwned  = true;
    }
    else if (auto proxyIt = proxyTable.find(trail.data()); proxyIt != proxyTable.end())
    {
        isProxy = true; // Static entry
    }

    if (!isOwned && !isProxy)
        return;

    processing::PacketBuilder na(&iface);

    types::IPv6Address adv = trail.data();
    neighborAdvertisement(na, iface.configs.getMac(), &adv);

    if (srcIp == IPV6_SOURCE)
    {
        // DAD probe or missing MAC — send multicast NA
        ippacket::BuildIP build = {
            .iface        = &iface,
            .packetInfo   = na,
            .destIp       = IPV6_MULTICAST,
            .sourceIp     = targetIp,
            .protocolType = IP_ICMPV6
        };
        ippacket::buildIpv6(build);
    }
    else
    {
        // Unicast NA back to solicitor
        ippacket::BuildIP build = {
            .iface        = &iface,
            .packetInfo   = na,
            .destIp       = srcIp,
            .sourceIp     = targetIp,
            .destMac      = srcMac,
            .protocolType = IP_ICMPV6
        };
        ippacket::buildIpv6(build);
    }
}

void Ndp::receiveRouteAdvertisement(const packet::Icmpv6Header& receivedRA, types::IPv6Address sourceIp, types::Mac sourceMac)
{
    if (configs.get<config::Ndp::RA_SUPPRESS>().load()) return;

    if (configs.get<config::Ndp::DESTINATION_GUARD>().load())
    {
        auto mode = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::HOST_MODE_STRICT>().load();

        if (!mode) return; // BLOCK_ALL when strict

        if (!raGuardAllowedMacs.count(sourceMac))
        {
            auto& lastTime = raReceivedTimestamps[sourceMac.mac];
            auto rateLimit = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE>().load();
            auto interval  = std::chrono::milliseconds(1000 / std::max<uint16_t>(rateLimit, 1));
            auto now       = std::chrono::steady_clock::now();

            if (now - lastTime < interval) return;
            lastTime = now;
        }
    }

    uint16_t routerLifetime = utils::read<uint16_t>(receivedRA.getReserved() + 2);
    uint8_t  flags          = receivedRA.getReserved()[1];
    bool mFlag = flags & 0x80;
    bool oFlag = flags & 0x40;

    if (configs.get<config::Ndp::AUTOCONFIG_DEFAULT_ROUTE>().load())
    {
        uint32_t pid = (uint32_t)std::hash<__uint128_t>{}(sourceIp.addr);

        // Cancel existing timer for this router
        auto timerIt = defaultRouterTimers.find(sourceIp);
        if (timerIt != defaultRouterTimers.end())
        {
            scheduler.cancel(timerIt->second);
            defaultRouterTimers.erase(timerIt);
        }

        if (routerLifetime > 0)
        {
            auto* entry          = new core::RibEntry<__uint128_t>();
            entry->prefix        = 0;
            entry->length        = 0;
            entry->source        = core::RouteSource::DYNAMIC;
            entry->processId     = pid;
            entry->adminDistance = 1;
            entry->metric        = 0;
            entry->addNextHop(sourceIp.addr, iface.configs.key, 1);
            iface.getVRF()->getRib().addRoute(entry);

            defaultRouterTimers[sourceIp] = scheduler.postAfter(
                std::chrono::steady_clock::now() + std::chrono::seconds(routerLifetime),
                [this, sourceIp, pid](uint32_t) {
                    iface.getVRF()->getRib().removeRoute<__uint128_t>(
                        0, 0, core::RouteSource::DYNAMIC, pid);
                    defaultRouterTimers.erase(sourceIp);
                }
            );
        }
        else
        {
            // Lifetime 0: router is no longer a default router
            iface.getVRF()->getRib().removeRoute<__uint128_t>(
                0, 0, core::RouteSource::DYNAMIC, pid);
        }
    }

    std::vector<packet::TLV8Option> options;
    auto trail = receivedRA.getTrail();
    packet::parseIcmpv6Options(trail.data() + 8, trail.size() - 8, options);

    for (const auto& opt : options)
    {
        if (opt.type == ICMPV6_OPTION_NDP_PREFIX && opt.valueSize >= 30)
        {
            uint8_t  prefixLen         = opt.value[0];
            uint8_t  prefixFlags       = opt.value[1];
            bool     A                 = prefixFlags & 0x40;
            uint32_t validLifetime     = utils::read<uint32_t>(opt.value + 2);
            uint32_t preferredLifetime = utils::read<uint32_t>(opt.value + 6);

            if (preferredLifetime > validLifetime) continue;

            bool isExcluded = std::any_of(
                slaacExclusionPrefixes.begin(), slaacExclusionPrefixes.end(),
                [&](types::IPv6Address p) { return p.contains(opt.value + 14, prefixLen); }
            );
            if (isExcluded) continue;

            if (A && validLifetime > 0 && configs.get<config::Ndp::AUTOCONFIG_PREFIX>().load())
            {
                auto* slaacAddr              = new interface::InterfaceConfigs::IPv6State::IPv6Address();
                slaacAddr->prefix.prefixLength = prefixLen;
                slaacAddr->tentative           = true;
                slaacAddr->valid               = false;
                slaacAddr->globalTentative     = true;
                slaacAddr->globalValid         = false;
                slaacAddr->deprecated          = false;
                slaacAddr->preferredLifetime   = preferredLifetime;

                uint8_t mac[6];
                uint8_t slac[16];
                calculateEui64(slac, opt.value + 14, iface.configs.getMac(mac));
                slaacAddr->prefix.addr = utils::read<__uint128_t>(slac);

                iface.configs.ipv6.globalAddresses.push_back(slaacAddr);

                slaacAddr->expirationId = scheduler.postAfter(
                    std::chrono::steady_clock::now() + std::chrono::seconds(validLifetime),
                    [slaacAddr](uint32_t) {
                        slaacAddr->globalValid = false;
                        slaacAddr->expirationId = 0;
                    }
                );

                slaacAddr->preferedExpirationId = scheduler.postAfter(
                    std::chrono::steady_clock::now() + std::chrono::seconds(preferredLifetime),
                    [slaacAddr](uint32_t) {
                        slaacAddr->deprecated = true;
                        slaacAddr->preferedExpirationId = 0;
                    }
                );

                duplicateAddressDetection(*slaacAddr);
            }
        }
    }
}

void Ndp::receiveRedirectMessage(const packet::Icmpv6Header& redirect, types::IPv6Address sourceIp)
{
    auto trail = redirect.getTrail();
    types::IPv6Address betterNextHop = utils::read<__uint128_t>(trail.data());
    types::IPv6Address destinationIp = utils::read<__uint128_t>(trail.data() + 16);
    types::Mac nextHopMac;
    bool macFound = false;

    std::vector<packet::TLV8Option> options;
    packet::parseIcmpv6Options(trail.data() + 32, trail.size() - 32, options);

    for (const auto& opt : options)
    {
        if (opt.type == ICMPV6_OPTION_NDP_TARGET && opt.valueSize == 6)
        {
            nextHopMac = utils::read<uint64_t, 6>(opt.value);
            macFound   = true;
            break;
        }
    }

    if (macFound)
    {
        addNdpEntry(betterNextHop, nextHopMac);
        completeNdpEntry(betterNextHop, ndpCache[betterNextHop], nextHopMac);
    }
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

void Ndp::sendNeighborAdvertisement(types::Mac destMac, types::IPv6Address targetIp)
{
    if (configs.get<config::Ndp::BASE>().get().get<config::NdpBase::HOST_MODE_STRICT>().load()) return; // suppressNA

    auto now     = std::chrono::steady_clock::now();
    auto& lastTime = lastUnsolicitedNaTime[targetIp];
    if (now - lastTime < std::chrono::seconds(1)) return;
    lastTime = now;

    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder naPacket(&iface);
    neighborAdvertisement(naPacket, iface.configs.getMac(), &targetIp);

    ippacket::BuildIP build = {
        .iface        = &iface,
        .packetInfo   = naPacket,
        .destIp       = targetIp,
        .destMac      = destMac,
        .protocolType = IP_ICMPV6
    };
    ippacket::buildIpv6(build);
}

void Ndp::sendNeighborAdvertisement()
{
    types::IPv6Address targetIp = iface.configs.ipv6.getLocalAddress();

    auto now     = std::chrono::steady_clock::now();
    auto& lastTime = lastUnsolicitedNaTime[targetIp];
    if (now - lastTime < std::chrono::seconds(1)) return;
    lastTime = now;

    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder naPacket(&iface);
    neighborAdvertisement(naPacket, iface.configs.getMac(), nullptr);

    types::IPv6Address solicitedNode = generateMulticastSolicitationAddress(targetIp);

    ippacket::BuildIP build = {
        .iface        = &iface,
        .packetInfo   = naPacket,
        .destIp       = solicitedNode,
        .protocolType = IP_ICMPV6
    };
    ippacket::buildIpv6(build);
}

void Ndp::sendRouteSolicitation(types::IPv6Address targetIp)
{
    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder rsPacket(&iface);
    routeSolicitation(rsPacket, iface.configs.getMac());

    ippacket::BuildIP build = {
        .iface        = &iface,
        .packetInfo   = rsPacket,
        .destIp       = generateMulticastSolicitationAddress(targetIp),
        .protocolType = IP_ICMPV6
    };
    ippacket::buildIpv6(build);
}

void Ndp::sendRouteAdvertisement(types::Mac targetMac, types::IPv6Address targetIp)
{
    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder raPacket(&iface);
    routeAdvertisement(raPacket, iface.configs.getMac());

    ippacket::BuildIP build = {
        .iface        = &iface,
        .packetInfo   = raPacket,
        .destIp       = targetIp,
        .destMac      = targetMac,
        .protocolType = IP_ICMPV6
    };
    ippacket::buildIpv6(build);
}

void Ndp::sendRedirectMessage(types::IPv6Address targetIp, types::IPv6Address destinationIp)
{
    if (!configs.get<config::Ndp::BASE>().get().get<config::NdpBase::ROUTE_OWNER>().load()) return;
    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::PacketBuilder packet(&iface);
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0);

    packet::Icmpv6Header icmp;
    auto* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6) return;

    icmp.setBuffer(nextHeader->buffer);
    icmp.setType(ICMPV6_OPCODE_NDP_REDIRECT_MESSAGE);
    icmp.setCode(0);
    icmp.setReserved(0);

    uint8_t* trail = icmp.getTrailData();
    utils::write<__uint128_t>(trail,      destinationIp.addr);
    utils::write<__uint128_t>(trail + 16, targetIp.addr);

    packet::TLV8BufferManager options(trail + 32, 8);
    options.append(ICMPV6_OPTION_NDP_TARGET, 1, 0, 0);
    iface.configs.getMac(trail + 34);

    nextHeader->length = packet::Icmpv6Header::fixedSize + 32 + options.size();
    packet.bufferOffset += nextHeader->length;

    ippacket::BuildIP build = {
        .iface        = &iface,
        .packetInfo   = packet,
        .destIp       = destinationIp,
        .protocolType = IP_ICMPV6
    };
    ippacket::buildIpv6(build);
}

void Ndp::sendRedirectIfNeeded(const packet::PacketInfo& originalPacket, const uint8_t* pkt)
{
    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;
    if (!configs.get<config::Ndp::BASE>().get().get<config::NdpBase::ROUTE_OWNER>().load()) return;

    // Find the IPv6 header
    packet::IPv6HeaderRaw* ipv6 = nullptr;
    for (size_t i = 0; i < originalPacket.count; ++i)
    {
        if (originalPacket.headers[i].type == packet::HeaderType::IPV6)
        {
            ipv6 = reinterpret_cast<packet::IPv6HeaderRaw*>(
                const_cast<uint8_t*>(pkt) + originalPacket.headers[i].offset);
            break;
        }
    }
    if (!ipv6) return;

    types::IPv6Address srcIp { utils::read<__uint128_t>(ipv6->sourceAddress)      };
    types::IPv6Address dstIp { utils::read<__uint128_t>(ipv6->destinationAddress) };

    // Helper: check if addr falls within prefix/prefixLen
    auto isOnLink = [](const uint8_t* addr, const uint8_t* prefix, uint8_t prefixLen) {
        uint8_t full = prefixLen / 8;
        uint8_t rem  = prefixLen % 8;
        if (std::memcmp(addr, prefix, full) != 0) return false;
        if (rem == 0) return true;
        uint8_t mask = static_cast<uint8_t>(0xFF << (8 - rem));
        return (addr[full] & mask) == (prefix[full] & mask);
    };

    // Source must be on one of our local prefixes (on-link sender)
    bool srcOnLink = false;
    for (const auto& addr : iface.configs.ipv6.globalAddresses)
    {
        if (!addr->valid) continue;
        uint8_t pfxBytes[16];
        utils::write<__uint128_t>(pfxBytes, addr->prefix.addr);
        uint8_t srcBytes[16];
        utils::write<__uint128_t>(srcBytes, srcIp.addr);
        if (isOnLink(srcBytes, pfxBytes, addr->prefix.prefixLength))
        {
            srcOnLink = true;
            break;
        }
    }
    if (!srcOnLink) return;

    // Look up best route for destination
    utils::RCU::Guard g;
    auto* entry = iface.getVRF()->getRib().lookup<__uint128_t>(dstIp.addr, g);
    if (!entry || entry->nextHopCount == 0 || !entry->nextHops[0].nextHop.has_value()) return;

    // Next-hop must exit via the same interface (on-link next-hop)
    if (entry->nextHops[0].iface != iface.configs.key) return;

    types::IPv6Address betterNextHop { *entry->nextHops[0].nextHop };

    // Don't redirect back to the source
    if (betterNextHop == srcIp) return;

    sendRedirectMessage(betterNextHop, dstIp);
}

// ---------------------------------------------------------------------------
// RA scheduling
// ---------------------------------------------------------------------------

void Ndp::scheduleNextRA()
{
    if (!running.load(std::memory_order_relaxed)) return;

    // Prune stale RA timestamps
    auto now = std::chrono::steady_clock::now();
    for (auto it = raReceivedTimestamps.begin(); it != raReceivedTimestamps.end();)
    {
        if (now - it->second > std::chrono::minutes(10))
            it = raReceivedTimestamps.erase(it);
        else
            ++it;
    }

    uint32_t baseInterval = configs.get<config::Ndp::RA_INTERVAL>().load();
    if (configs.get<config::Ndp::ADVERTISEMENT_INTERVAL>().load())
    {
        uint32_t minInterval = configs.get<config::Ndp::RA_MIN_INTERVAL>().load();
        if (minInterval > baseInterval) minInterval = baseInterval;
        uint32_t delta = baseInterval - minInterval;
        baseInterval   = minInterval + (static_cast<uint32_t>(rand()) % (delta + 1));
    }

    uint32_t raTimerId = scheduler.postAfter(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(baseInterval),
        [this](uint32_t timerId)
        {
            raTimerIds.erase(timerId);

            if (!running.load(std::memory_order_relaxed) ||
                configs.get<config::Ndp::RA_SUPPRESS>().load())
                return;

            auto localAddr = iface.configs.ipv6.getLocalAddress();
            if (localAddr.addr)
                sendRouteAdvertisement(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), localAddr);

            scheduleNextRA();
        }
    );
    raTimerIds.insert(raTimerId);
}

// ---------------------------------------------------------------------------
// SLAAC / DAD
// ---------------------------------------------------------------------------

void Ndp::initiateSlaac()
{
    if (!configs.get<config::Ndp::AUTOCONFIG_PREFIX>().load()) return;

    processing::PacketBuilder rs(&iface);
    routeSolicitation(rs, iface.configs.getMac());

    ippacket::BuildIP build = {
        .iface        = &iface,
        .packetInfo   = rs,
        .destIp       = ICMPV6_ALL_ROUTERS,
        .protocolType = IP_ICMPV6
    };
    ippacket::buildIpv6(build);
}

void Ndp::duplicateAddressDetection(interface::InterfaceConfigs::IPv6State::IPv6Address& addr)
{
    if (iface.shutdownFlag.load(std::memory_order_relaxed)) return;
    if (configs.get<config::Ndp::DAD_ATTEMPTS>().load() == 0) return;
    if (!addr.tentative) return;

    auto& entry = dadEntries[addr.prefix];

    entry.retires = 0;
    entry.duplicate = false;

    preformDad(addr);
}

void Ndp::preformDad(interface::InterfaceConfigs::IPv6State::IPv6Address& addr)
{
    auto entry = dadEntries.find(addr.prefix);
    if (entry == dadEntries.end())
        return;

    const uint32_t maxAttempts = configs.get<config::Ndp::DAD_ATTEMPTS>().load();
    const auto delay       = std::chrono::milliseconds(
    configs.get<config::Ndp::BASE>().get().get<config::NdpBase::DAD_TIME>().load());

    uint32_t attempt = entry->second.retires;
    bool isDuplicate = entry->second.duplicate;
    bool done        = false;

    if (isDuplicate)
    {
        addr.tentative = false;
        addr.valid     = false;
        iface.markAddressDuplicate(addr.prefix);
        done = true;
    }
    else if (attempt >= maxAttempts)
    {
        addr.tentative = false;
        addr.valid     = true;
        iface.setIPv6Ready(addr.prefix);
        done = true;
    }
    else
    {
        // Send anonymous NS (source = ::, no MAC option — DAD probe)
        processing::PacketBuilder ns(&iface);
        neighborSolicitation(ns, addr.prefix, nullptr);

        ippacket::BuildIP build = {
            .iface        = &iface,
            .packetInfo   = ns,
            .destIp       = generateMulticastSolicitationAddress(addr.prefix),
            .sourceIp     = IPV6_SOURCE,
            .protocolType = IP_ICMPV6
        };
        ippacket::buildIpv6(build);

        entry->second.retires++;

        entry->second.timerId = scheduler.postAfter(
            std::chrono::steady_clock::now() + delay,
            [this, &addr](uint32_t) { preformDad(addr); }
        );
    }

    if (done)
    {
        dadEntries.erase(entry);
    }
}

// ---------------------------------------------------------------------------
// RA Guard / SLAAC exclusion helpers
// ---------------------------------------------------------------------------

void Ndp::addSlaacExclusionPrefix(types::IPv6Address prefix, bool remove)
{
    if (remove)
        std::erase_if(slaacExclusionPrefixes,
                      [&](types::IPv6Address p) { return p == prefix; });
    else if (!std::any_of(slaacExclusionPrefixes.begin(), slaacExclusionPrefixes.end(),
                          [&](types::IPv6Address p) { return p == prefix; }))
        slaacExclusionPrefixes.push_back(prefix);
}

void Ndp::addRaGuardAllowedMac(types::Mac mac, bool remove)
{
    if (remove)
        raGuardAllowedMacs.erase(mac);
    else
        raGuardAllowedMacs.insert(mac);
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

bool Ndp::shouldLog()
{
    auto now  = std::chrono::steady_clock::now();
    uint16_t rate = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::CACHE_INTERFACE_LIMIT_LOG_RATE>().load();
    if (rate == 0) return true;

    auto minInterval = std::chrono::microseconds(1'000'000 / rate);
    static std::chrono::steady_clock::time_point lastLog;
    if (now - lastLog >= minInterval)
    {
        lastLog = now;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// ICMPv6 packet builders
// ---------------------------------------------------------------------------

types::IPv6Address Ndp::generateMulticastSolicitationAddress(types::IPv6Address targetIp)
{
    types::IPv6Address out(ICMPV6_SOLICIT_MULTICAST);
    out.raw()[13] = targetIp.raw()[13];
    out.raw()[14] = targetIp.raw()[14];
    out.raw()[15] = targetIp.raw()[15];
    return out;
}

void Ndp::neighborSolicitation(processing::PacketBuilder& packet, types::IPv6Address targetIp, uint64_t* currentMac)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0);

    packet::Icmpv6Header icmp;
    auto* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6) return;

    icmp.setBuffer(nextHeader->buffer);
    icmp.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_SOLICITATION);
    icmp.setCode(0);
    icmp.setReservedInt(0);

    uint8_t* trail = icmp.getTrailData();
    utils::write<__uint128_t>(trail, targetIp.addr);

    if (currentMac)
    {
        packet::TLV8BufferManager options(trail + 16, 8);
        uint8_t* buf = options.getNextValBuf(6);
        utils::write<uint64_t, 6>(buf, *currentMac);
        options.append(ICMPV6_OPTION_NDP_SOURCE, 1, nullptr, 6);
        nextHeader->length = packet::Icmpv6Header::fixedSize + 16 + options.size();
    }
    else
    {
        nextHeader->length = packet::Icmpv6Header::fixedSize + 16;
    }

    packet.bufferOffset += nextHeader->length;
}

void Ndp::neighborAdvertisement(processing::PacketBuilder& packet, types::Mac currentMac, types::IPv6Address* targetIp)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0);

    packet::Icmpv6Header icmp;
    auto* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6) return;

    icmp.setBuffer(nextHeader->buffer);
    icmp.setType(ICMPV6_OPCODE_NDP_NEIGHBOR_ADVERTISEMENT);
    icmp.setCode(0);

    uint8_t reserved[4] = {};
    reserved[0] = 0x80 | (targetIp ? 0x40 : 0) | 0x20; // R=1, S=solicited, O=override
    icmp.setReserved(reserved);

    uint8_t* trail = icmp.getTrailData();
    if (targetIp)
        utils::write<__uint128_t>(trail, targetIp->addr);
    else
        utils::write<__uint128_t>(trail, iface.configs.ipv6.getLocalAddress().addr);

    packet::TLV8BufferManager options(trail + 16, 8);
    uint8_t* buf = options.getNextValBuf(6);
    utils::write<uint64_t, 6>(buf, currentMac);
    options.append(ICMPV6_OPTION_NDP_TARGET, 1, nullptr, 6);

    nextHeader->length = packet::Icmpv6Header::fixedSize + 16 + options.size();
    packet.bufferOffset += nextHeader->length;
}

void Ndp::routeSolicitation(processing::PacketBuilder& packet, types::Mac currentMac)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0);

    packet::Icmpv6Header icmp;
    auto* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6) return;

    icmp.setBuffer(nextHeader->buffer);
    icmp.setType(ICMPV6_OPCODE_NDP_ROUTE_SOLICITATION);
    icmp.setCode(0);
    icmp.setReservedInt(0);

    uint8_t* trail = icmp.getTrailData();
    packet::TLV8BufferManager options(trail, 8);
    uint8_t* buf = options.getNextValBuf(6);
    utils::write<uint64_t, 6>(buf, currentMac);
    options.append(ICMPV6_OPTION_NDP_SOURCE, 1, nullptr, 6);

    nextHeader->length = packet::Icmpv6Header::fixedSize + options.size();
    packet.bufferOffset += nextHeader->length;
}

void Ndp::routeAdvertisement(processing::PacketBuilder& packet, types::Mac currentMac)
{
    ippacket::reserveIpv6(packet);
    packet.reserveHeader(packet::HeaderType::ICMPV6, 0);

    packet::Icmpv6Header icmp;
    auto* nextHeader = packet.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::ICMPV6) return;

    icmp.setBuffer(nextHeader->buffer);
    icmp.setType(ICMPV6_OPCODE_NDP_ROUTE_ADVERTISEMENT);
    icmp.setCode(0);

    uint8_t reserved[4] = {};
    if (configs.get<config::Ndp::MANAGED_CONFIG_FLAG>().load()) reserved[1] |= 0x80;
    if (configs.get<config::Ndp::OTHER_CONFIG_FLAG>().load())   reserved[1] |= 0x40;

    switch (configs.get<config::Ndp::ROUTER_PREFERENCE>().load())
    {
        case config::ndp::Preference::LOW:  reserved[1] |= 0x18; break; // 11
        case config::ndp::Preference::HIGH: reserved[1] |= 0x08; break; // 01
        default: break;                                                   // 00 = MEDIUM
    }

    reserved[0] |= configs.get<config::Ndp::RA_HOP_LIMIT_UNSPECIFIED>().load() ? 0 : 64;
    utils::write<uint16_t>(reserved + 2, configs.get<config::Ndp::RA_LIFETIME>().load());
    icmp.setReserved(reserved);

    uint8_t* trail = icmp.getTrailData();
    uint32_t reachableTime = configs.get<config::Ndp::BASE>().get().get<config::NdpBase::REACHABLE_TIME>().load();
    utils::write<uint32_t>(trail,     reachableTime);
    utils::write<uint32_t>(trail + 4, 0); // retrans timer — let neighbor use its own

    packet::TLV8BufferManager options(trail + 8);
    uint8_t* buf = options.getNextValBuf(6);
    utils::write<uint64_t, 6>(buf, currentMac);
    options.append(ICMPV6_OPTION_NDP_SOURCE, 1, nullptr, 6);

    if (!configs.get<config::Ndp::RA_MTU_SUPPRESS>().load())
    {
        uint8_t mtu[6] = {};
        utils::write<uint32_t>(mtu + 2, iface.configs.ipv6.mtu.load(std::memory_order_relaxed));
        options.append(ICMPV6_OPTION_NDP_MTU, 1, mtu, 6);
    }

    if (configs.get<config::Ndp::AUTOCONFIG_PREFIX>().load())
    {
        for (const auto& addr : iface.configs.ipv6.globalAddresses)
        {
            if (!addr->valid) continue;

            const uint8_t prefixLen = addr->prefix.prefixLength;
            uint8_t flags = 0xC0; // L=1 (on-link), A=1 (autonomous)

            uint32_t lifetime          = configs.get<config::Ndp::RA_LIFETIME>().load();
            uint32_t preferredLifetime = lifetime / 2;

            uint8_t value[30] = {};
            value[0] = prefixLen;
            value[1] = flags;
            utils::write<uint32_t>(value + 2,  lifetime);
            utils::write<uint32_t>(value + 6,  preferredLifetime);
            utils::write<__uint128_t>(value + 14, types::IPv6Address{addr->prefix.addr, prefixLen}.addr);

            options.append(ICMPV6_OPTION_NDP_PREFIX, 4, value, 30);
        }
    }

    nextHeader->length = packet::Icmpv6Header::fixedSize + 8 + options.size();
    packet.bufferOffset += nextHeader->length;
}

} // namespace infrastructure
