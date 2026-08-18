// Interface.cpp

#include <mutex>
#include <Global.h>
#include <VirtualRouter.h>

#include "Interface.h"
#include "configs/InterfaceConfigs.h"
#include "dhcp/dhcpv4/DhcpClient.h"
//#include <Dhcpv6.h>

#include "infrastructure/Arp.h"
#include "infrastructure/Ndp.h"
#include "processing/Decapsulation.h"
#include "processing/Encapsulation.h"
#include "processing/Process.h"
#include "processing/PacketBuilder.hpp"
#include "qos/egress/TxQueueManager.h"
#include "qos/ingress/RxQueueManager.h"
#include "processing/Process.h"
#include "hardware/HardwareManager.h"
#include "InterfaceManager.h"

#include "eigrp/core/Eigrp.h"
#include "ospf/OspfProcess.h"

namespace interface
{
Interface::Interface(const InterfaceCreation& cfgs)
  : routingInstance(&cfgs.vrf),
    scheduler(cfgs.vrf.getControlScheduler().create()),
    configs(*this, cfgs.interfaceType, cfgs.interfaceId, cfgs.info, cfgs.cfg),
    arp(*this),
    ndp(*this),
    debug(cfgs.debug),
    threadsRunning(false)
{
    if (!debug)
    {
        cfgs.vrf.getGlobal().txManager.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
        cfgs.vrf.getGlobal().rxManager.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
        cfgs.vrf.getGlobal().hwManager.registerInterface(&configs.hwInfo, this);
    }
}

Interface::~Interface()
{
    cleanupInterface();
    if (!debug)
    {
        core::VirtualRouter* vrf = getVRF();
        vrf->getGlobal().txManager.removeInterface(*this);
        vrf->getGlobal().rxManager.removeInterface(*this);
        vrf->getGlobal().hwManager.unregisterInterface(&configs.hwInfo, this);
    }
}

void Interface::cleanupInterface()
{
    shutdown(true);
    core::VirtualRouter* vrf = getVRF();
    
    if (dhcp) delete dhcp;

    // Remove interface from list
    if (routingInstance)
    {
        vrf->getInterfaceManager().remove(configs.key);
    }
}

bool Interface::setIPv4(types::IPv4Prefix prefix, bool secondary)
{
    auto sendGratuitous = [&]()
    {
        if (!arp.isShutdown())
        {
            types::IPv4Address v4addr(prefix.addr);
            arp.sendReply(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), v4addr);
            arp.sendReply(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), v4addr);
        }
    };

    if (!secondary)
    {
        configs.ipv4.setPrimaryAddress(prefix);
        sendGratuitous();
        getVRF()->getInterfaceManager().notify(IPv4Event::IPV4_READY, *this, prefix);
    }
    else
    {
        configs.ipv4.addSecondaryAddress(prefix);
        sendGratuitous();
        getVRF()->getInterfaceManager().notify(IPv4Event::IPV4_SECONDARY_READY, *this, prefix);
    }
    applyConnectedRoute(prefix);
    return true;
}

bool Interface::setIPv6(const types::IPv6Prefix& addr, bool eui64)
{
    InterfaceConfigs::IPv6State::IPv6Address* ipv6 = nullptr;
    types::IPv6Address ip{addr};

    if (ip.isLocalLink())
        ipv6 = configs.ipv6.addAddress(addr, true);
    else if (ip.isLocalUnicast())
        ipv6 = configs.ipv6.addUniqueLocalAddress(addr);
    else if (ip.isGlobalUnicast())
        ipv6 = configs.ipv6.addAddress(addr, false);
    else
        return false; // invalid

    // Run Duplicate Address Detection (dad) using NDP
    if (ipv6)
    {
        ndp.duplicateAddressDetection(*ipv6);
    }
    else
    {
        //TODO duplicate address error
        return false;
    }

    if (!ip.isLocalLink())
        applyConnectedRoute(addr);

    return true;
}

void Interface::setIPv6Ready(const types::IPv6Prefix& addr)
{
    if (addr.isLocalLink())
        getVRF()->getInterfaceManager().notify(IPv6Event::IPV6_LL_READY, *this, addr);
    else
        getVRF()->getInterfaceManager().notify(IPv6Event::IPV6_READY, *this, addr);
}

void Interface::removeIPv4(const types::IPv4Prefix* prefix)
{
    types::IPv4Prefix removed;

    if (!prefix)
    {
        removed = configs.ipv4.getPrimaryPrefix();
        configs.ipv4.removePrimaryAddress();
        stateChangeV4(IPv4Event::IPV4_DEL, removed);
    }
    else
    {
        removed = *prefix;
        configs.ipv4.removeSecondaryAddress(*prefix);
        stateChangeV4(IPv4Event::IPV4_DEL, *prefix);
    }

    removeConnectedRoute(removed);
}

void Interface::removeAllIPv4()
{
    // Primary
    types::IPv4Prefix removed = configs.ipv4.getPrimaryPrefix();
    configs.ipv4.removePrimaryAddress();
    stateChangeV4(IPv4Event::IPV4_DEL, removed);

    // Secondary
    std::vector<types::IPv4Prefix> secondary = configs.ipv4.getSecondaryPrefixList(true);
    configs.ipv4.clearSecondaryAddresses();
    for (const auto& ip : secondary)
        stateChangeV4(IPv4Event::IPV4_DEL, ip);

    removeAllConnectedRoutes<types::IPv4Prefix>();
}

void Interface::removeIPv6(const types::IPv6Prefix* prefix)
{
    if (prefix)
    {
        configs.ipv6.removeAddress(*prefix);
        removeConnectedRoute(*prefix);
        stateChangeV6(IPv6Event::IPV6_DEL, *prefix);

        if (!types::IPv6Address(*prefix).isLocalLink())
        {
            types::IPv6Prefix network(prefix->addr, prefix->prefixLength);
            getVRF()->getRib().removeRoute<__uint128_t>(network.addr, network.prefixLength, core::RouteSource::CONNECTED, 0);
        }
    }
    else
    {
        types::IPv6Prefix ll = configs.ipv6.getLocalPrefix();
        configs.ipv6.removeLocalAddress();
        stateChangeV6(IPv6Event::IPV6_LL_DEL, ll);
    }
}

void Interface::removeAllIPv6(bool local)
{
    if (local) // Local Link
    {
        types::IPv6Prefix llAddr = configs.ipv6.getLocalPrefix();
        configs.ipv6.removeLocalAddress();
        stateChangeV6(IPv6Event::IPV6_LL_DEL, llAddr);
    }

    // Routable
    std::vector<types::IPv6Prefix> routable = configs.ipv6.getRoutablePrefixList(true);
    for (const auto& addr : routable)
        stateChangeV6(IPv6Event::IPV6_DEL, addr);

    removeAllConnectedRoutes<types::IPv6Prefix>();
}

std::vector<std::array<uint8_t, 16>> Interface::getTentativeAddress()
{
    std::vector<std::array<uint8_t, 16>> tentative;
    std::lock_guard<std::mutex> lock(configs.ipv6.ipMutex);

    // Link-local (there can only be one)
    if (!configs.ipv6.linkLocalAddress->valid && configs.ipv6.linkLocalAddress->tentative)
    {
        tentative.emplace_back();
        utils::write<__uint128_t>(tentative.back().data(), configs.ipv6.linkLocalAddress->prefix.addr);
    }

    // core::Global unicast
    for (const auto& addr : configs.ipv6.globalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            utils::write<__uint128_t>(tentative.back().data(), addr->prefix.addr);
        }
    }

    // Unique local
    for (const auto& addr : configs.ipv6.uniqueLocalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            utils::write<__uint128_t>(tentative.back().data(), addr->prefix.addr);
        }
    }

    return tentative;
}

void Interface::markAddressDuplicate(types::IPv6Prefix address)
{
    if (address.isLocalLink() && configs.ipv6.getLocalAddress() == address.addr)
    {
        getVRF()->getInterfaceManager().notify(IPv6Event::IPV6_LL_CONFLICT, *this, address);
        configs.ipv6.linkLocalAddress->valid = false;
    }
    else
    {
        // Mark the address invalid in place, matching the link-local branch above --
        // do NOT erase it from the list. The IPv6Address object is still owned by
        // globalAddresses/uniqueLocalAddresses and freed by IPv6State's own cleanup
        // (destructor / removeAddress / removeAllAddresses); erasing it here without
        // deleting orphaned the pointer and leaked it, while callers (e.g. DAD) still
        // hold and dereference the same object after this call returns.
        auto markInvalid = [&](std::vector<InterfaceConfigs::IPv6State::IPv6Address*>& list) {
            for (auto* entry : list)
            {
                if (entry->prefix.addr == address.addr)
                {
                    entry->valid = false;
                    return;
                }
            }
        };
        markInvalid(configs.ipv6.globalAddresses);
        markInvalid(configs.ipv6.uniqueLocalAddresses);
    }
}

template <types::IsIPPrefix Prefix>
void Interface::applyConnectedRoute(Prefix network)
{
    network.addPrefixLen(network.prefixLength);

    auto* entry = new core::RibEntry<decltype(network.addr)>();
    entry->prefix = network.addr;
    entry->length = network.prefixLength;
    entry->source = core::RouteSource::CONNECTED;
    entry->processId = 0;
    entry->adminDistance = 0;
    entry->metric = 0;
    entry->addNextHopInterface(configs.key.getId());
    getVRF()->getRib().addRoute(entry);
}

void Interface::applyAllConnectedRoutes()
{
    applyAllConnectedRoutes<types::IPv4Prefix>();
    applyAllConnectedRoutes<types::IPv6Prefix>();
}

template <>
void Interface::applyAllConnectedRoutes<types::IPv4Prefix>()
{
    auto& rib = getVRF()->getRib();

    std::vector<core::RibEntry<uint32_t>*> connected;

    if (configs.getConfigs().get<config::Interface::IP_ADDRESS>().hasValue())
    {
        auto primaryAddr = configs.ipv4.getPrimaryPrefix(false);

        auto* entry = new core::RibEntry<uint32_t>();
        entry->prefix = primaryAddr.addr;
        entry->length = primaryAddr.prefixLength;
        entry->source = core::RouteSource::CONNECTED;
        entry->processId = 0;
        entry->adminDistance = 0;
        entry->metric = 0;
        entry->addNextHopInterface(configs.key.getId());
        connected.push_back(entry);
    }

    for (const auto& network : configs.ipv4.getSecondaryPrefixList(false))
    {
        auto* entry = new core::RibEntry<decltype(network.addr)>();
        entry->prefix = network.addr;
        entry->length = network.prefixLength;
        entry->source = core::RouteSource::CONNECTED;
        entry->processId = 0;
        entry->adminDistance = 0;
        entry->metric = 0;
        entry->addNextHopInterface(configs.key.getId());
        connected.push_back(entry);
    }

    rib.addRoutes(connected);
}

template <>
void Interface::applyAllConnectedRoutes<types::IPv6Prefix>()
{
    auto& rib = getVRF()->getRib();

    std::vector<core::RibEntry<__uint128_t>*> connected;
    for (const auto& network : configs.ipv6.getRoutablePrefixList(false))
    {
        auto* entry = new core::RibEntry<decltype(network.addr)>();
        entry->prefix = network.addr;
        entry->length = network.prefixLength;
        entry->source = core::RouteSource::CONNECTED;
        entry->processId = 0;
        entry->adminDistance = 0;
        entry->metric = 0;
        entry->addNextHopInterface(configs.key.getId());
        connected.push_back(entry);
    }

    rib.addRoutes(connected);
}

template <types::IsIPPrefix Prefix>
void Interface::removeConnectedRoute(Prefix network)
{
    network.addPrefixLen(network.prefixLength);

    getVRF()->getRib().removeRoute(network.addr, network.prefixLength, core::RouteSource::CONNECTED, 0);
}

void Interface::removeAllConnectedRoutes()
{
    removeAllConnectedRoutes<types::IPv4Prefix>();
    removeAllConnectedRoutes<types::IPv6Prefix>();
}

template <> 
void Interface::removeAllConnectedRoutes<types::IPv4Prefix>()
{
    auto& rib = getVRF()->getRib();

    // Remove primary
    auto primaryAddr = configs.ipv4.getPrimaryPrefix(false);
    rib.removeRoute(primaryAddr.addr, primaryAddr.prefixLength, core::RouteSource::CONNECTED, 0);
    // Remove secondary
    rib.removeRoutes(configs.ipv4.getSecondaryPrefixList(false), core::RouteSource::CONNECTED, 0);
}

template <>
void Interface::removeAllConnectedRoutes<types::IPv6Prefix>()
{
    auto& rib = getVRF()->getRib();
    // Remove routable
    rib.removeRoutes(configs.ipv6.getRoutablePrefixList(false), core::RouteSource::CONNECTED, 0);
}

void Interface::shutdown(bool shut) 
{
    if (shutdownFlag.load(std::memory_order_relaxed) == shut)
        return;
    shutdownFlag.store(shut, std::memory_order_release);
    if (shut) 
    {
        if (dhcp) dhcp->shutdown();
        arp.shutdown();
        if (getVRF()->global.isIPv6UnicastRouting())
        {
            // DHCPV6
            ndp.shutdown();
        }

        removeAllConnectedRoutes();
        getVRF()->getInterfaceManager().notify(StateChange::IF_DOWN, *this);
    }
    else if (!shut) 
    {
        if (dhcp) dhcp->initiate();
        arp.refresh();
        if (getVRF()->global.isIPv6UnicastRouting())
        {
            // DHCPV6
            ndp.refresh();
        }

        applyAllConnectedRoutes();
        getVRF()->getInterfaceManager().notify(StateChange::IF_READY, *this);
    }
}

void Interface::reset()
{
    arp.refresh();
    if (getVRF()->global.isIPv6UnicastRouting())
        ndp.refresh();
}

void Interface::physicalShutdown(bool shut)
{
    if (carrierFlag.load(std::memory_order_relaxed) == !shut) return;
    carrierFlag.store(!shut, std::memory_order_release);

    if (!shut) startThreads();
    shutdown(shut);
    if (shut) startThreads();
}

void Interface::enqueuePacket(processing::PacketBuilder& packetInfo, uint64_t mac)
{
    if (!threadsRunning.load(std::memory_order_relaxed)) return;

    if (!encapsulate(packetInfo))
        return;

    utils::write<uint64_t, 6>(packetInfo.getBuffer(), mac);

    // Enqueue the serialized packet for sending
    if (packetInfo.frame.slot)
    {
        tx->push(packetInfo.frame.slot);
    }
}

void Interface::enqueuePacket(processing::PacketBuilder& packetInfo)
{
    if (!threadsRunning.load(std::memory_order_relaxed)) return;

    if (!encapsulate(packetInfo))
        return;

    // Enqueue the serialized packet for sending
    if (packetInfo.frame.slot)
    {
        tx->push(packetInfo.frame.slot);
    }
}

void Interface::processIngress(uint8_t* packet, size_t size)
{
    rxFrames.fetch_add(1, std::memory_order_relaxed);
    processing::PacketInfo packetInfo;
    processing::inspect(packetInfo, packet, size);
    processing::decapsulate(packetInfo, packet, size);
    processing::processPacket(packet, size, packetInfo, routingInstance, this);
}

void Interface::startThreads() 
{
    // Add the interface to the TX Queue manager
    core::VirtualRouter* vrf = getVRF();
    vrf->getGlobal().txManager.start(this);
    vrf->getGlobal().rxManager.start(this);
    vrf->getGlobal().hwManager.bringUp(configs.hwInfo.ifname);

    threadsRunning = true;

    //ingress->start();
    //TODO
}

void Interface::stopThreads() 
{
    // Add the interface to the TX Queue manager
    core::VirtualRouter* vrf = getVRF();
    vrf->getGlobal().txManager.stop(this);
    vrf->getGlobal().rxManager.stop(this);
    threadsRunning.store(false, std::memory_order_release); 
}

void Interface::stateChangeV4(IPv4Event state, types::IPv4Prefix addr)
{
    // TODO: add refresh() to arp and run that.
    getVRF()->getInterfaceManager().notify(state, *this, addr);
}

void Interface::stateChangeV6(IPv6Event state, types::IPv6Prefix addr)
{
    // TODO: add refresh() to ndp and run that.
    getVRF()->getInterfaceManager().notify(state, *this, addr);
}

core::VirtualRouter* Interface::getVRF()
{
    return routingInstance.load(std::memory_order_relaxed);
}

bool Interface::setVRF(core::VirtualRouter* vrf)
{
    core::VirtualRouter* oldVrf = getVRF();
    if (oldVrf == vrf)
        return false;

    bool isShutdown = shutdownFlag.load(std::memory_order_relaxed);
    if (!isShutdown) shutdown(true);

    //TODO remove ipaddress configs
    removeAllIPv4();
    removeAllIPv6();

    getVRF()->getInterfaceManager().remove(configs.key);
    routingInstance.store(vrf, std::memory_order_release);
    vrf->getInterfaceManager().add(this, configs.key);

    if (!isShutdown) shutdown(false);

    return true;
}

template void Interface::applyConnectedRoute<types::IPv4Prefix>(types::IPv4Prefix);
template void Interface::applyConnectedRoute<types::IPv6Prefix>(types::IPv6Prefix);
template void Interface::removeConnectedRoute<types::IPv4Prefix>(types::IPv4Prefix);
template void Interface::removeConnectedRoute<types::IPv6Prefix>(types::IPv6Prefix);
} // namespace interface
