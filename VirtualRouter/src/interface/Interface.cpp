// Interface.cpp

#include <mutex>
#include <Global.h>
#include <VirtualRouter.h>

#include "Interface.h"
#include "configs/InterfaceConfigs.h"
#include "dhcp/dhcpv4/DhcpClient.h"
//#include <Dhcpv6.h>

#include "core/routing/rib/RouteSource.hpp"
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
#include "packet/headers/EthernetHeader.hpp"

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
    cfgs.vrf.getInterfaceManager().add(this, configs.key);

    if (!debug)
    {
        cfgs.vrf.getGlobal().txManager.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
        cfgs.vrf.getGlobal().rxManager.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
        cfgs.vrf.getGlobal().hwManager.registerInterface(&configs.hwInfo, this);
    }

    physicalShutdown(false);
}

void Interface::drainQueuedPackets()
{
    // Drain queued PacketBuilders before tx is deleted; ~Arp()/~Ndp() run too late.
    arp.clear();
    ndp.clear();
}

Interface::~Interface()
{
    cleanupInterface();

    drainQueuedPackets();

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
    physicalShutdown(true);
    core::VirtualRouter* vrf = getVRF();
    
    //if (dhcp) delete dhcp;

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
        stateChange(IPEvent::IPV4_READY, prefix);
    }
    else
    {
        configs.ipv4.addSecondaryAddress(prefix);
        sendGratuitous();
        stateChange(IPEvent::IPV4_SECONDARY_READY, prefix);
    }

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

    return true;
}

void Interface::setIPv6Ready(const types::IPv6Prefix& addr)
{
    if (addr.isLocalLink())
        stateChange(IPEvent::IPV6_LL_READY, addr);
    else
        stateChange(IPEvent::IPV6_READY, addr);
}

void Interface::removeIPv4(const types::IPv4Prefix* prefix)
{
    types::IPv4Prefix removed;

    if (!prefix)
    {
        removed = configs.ipv4.getPrimaryPrefix();
        configs.ipv4.removePrimaryAddress();
        stateChange(IPEvent::IPV4_DEL, removed);
    }
    else
    {
        removed = *prefix;
        configs.ipv4.removeSecondaryAddress(*prefix);
        stateChange(IPEvent::IPV4_DEL, *prefix);
    }
}

void Interface::removeAllIPv4()
{
    // Primary
    types::IPv4Prefix removed = configs.ipv4.getPrimaryPrefix();
    configs.ipv4.removePrimaryAddress();
    stateChange(IPEvent::IPV4_DEL, removed);

    // Secondary
    std::vector<types::IPv4Prefix> secondary = configs.ipv4.getSecondaryPrefixList(true);
    configs.ipv4.clearSecondaryAddresses();
    for (const auto& ip : secondary)
        stateChange(IPEvent::IPV4_DEL, ip);
}

void Interface::removeIPv6(const types::IPv6Prefix* prefix)
{
    if (prefix)
    {
        configs.ipv6.removeAddress(*prefix);
        stateChange(IPEvent::IPV6_DEL, *prefix);

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
        stateChange(IPEvent::IPV6_LL_DEL, ll);
    }
}

void Interface::removeAllIPv6(bool local)
{
    if (local) // Local Link
    {
        types::IPv6Prefix llAddr = configs.ipv6.getLocalPrefix();
        configs.ipv6.removeLocalAddress();
        stateChange(IPEvent::IPV6_LL_DEL, llAddr);
    }

    // Routable
    std::vector<types::IPv6Prefix> routable = configs.ipv6.getRoutablePrefixList(true);
    for (const auto& addr : routable)
        stateChange(IPEvent::IPV6_DEL, addr);
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
        stateChange(IPEvent::IPV6_LL_CONFLICT, address);
        configs.ipv6.linkLocalAddress->valid = false;
    }
    else
    {
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

void Interface::shutdown(bool shut) 
{
    if (shutdownFlag.load(std::memory_order_relaxed) == shut)
        return;
    shutdownFlag.store(shut, std::memory_order_release);

    if (shut)
    {
        arp.shutdown();
        if (getVRF()->global.isIPv6UnicastRouting()) ndp.shutdown();
        stateChange(StateChange::IF_DOWN);
    }
    else if (!shut)
    {
        arp.initiateArp();
        if (getVRF()->global.isIPv6UnicastRouting()) ndp.initiateNdp();
        reannounceAddresses();
        stateChange(StateChange::IF_READY);
    }
}

void Interface::reannounceAddresses()
{
    if (!arp.isShutdown())
    {
        if (configs.ipv4.hasPrimaryAddress())
        {
            types::IPv4Address primary = configs.ipv4.getPrimaryAddress();
            arp.sendReply(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), primary);
            arp.sendReply(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), primary);
        }

        for (const types::IPv4Address& secondary : configs.ipv4.getSecondaryList())
        {
            arp.sendReply(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), secondary);
            arp.sendReply(utils::read<uint64_t, 6>(ETHERNET_MAC_BROADCAST), secondary);
        }
    }

    if (!ndp.isShutdown())
    {
        std::lock_guard<std::mutex> lock(configs.ipv6.ipMutex);

        if (configs.ipv6.linkLocalAddress && configs.ipv6.linkLocalAddress->tentative)
            ndp.duplicateAddressDetection(*configs.ipv6.linkLocalAddress);

        for (auto* addr : configs.ipv6.globalAddresses)
            if (addr && addr->tentative)
                ndp.duplicateAddressDetection(*addr);

        for (auto* addr : configs.ipv6.uniqueLocalAddresses)
            if (addr && addr->tentative)
                ndp.duplicateAddressDetection(*addr);
    }
}

void Interface::reset()
{
    arp.refresh();
    if (!ndp.isShutdown()) ndp.refresh();
}

void Interface::physicalShutdown(bool shut)
{
    if (carrierFlag.load(std::memory_order_relaxed) == !shut) return;
    carrierFlag.store(!shut, std::memory_order_release);

    if (!shut) startThreads();
    if (shut) stopThreads();
}

void Interface::enqueuePacket(processing::PacketBuilder& packetInfo, uint64_t mac)
{
    if (!threadsRunning.load(std::memory_order_relaxed)) return;

    if (!encapsulate(packetInfo))
        return;

    utils::write<uint64_t, 6>(packetInfo.getBuffer(), mac);

    // Enqueue the serialized packet for sending
    if (packetInfo.frame.slot && tx)
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
    if (packetInfo.frame.slot && tx)
    {
        tx->push(packetInfo.frame.slot);
    }
}

void Interface::processIngress(uint8_t* packet, size_t size)
{
    if (size >= sizeof(packet::EthernetHeaderRaw))
    {
        uint64_t srcMac = utils::read<uint64_t, 6>(packet + offsetof(packet::EthernetHeaderRaw, sourceMac));
        if (srcMac == static_cast<uint64_t>(configs.hwInfo.mac))
            return;
    }

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

void Interface::stateChange(IPEvent state, const types::IPPrefix& addr)
{
    getVRF()->getInterfaceManager().notify(state, *this, addr);
}

void Interface::stateChange(StateChange state)
{
    getVRF()->getInterfaceManager().notify(state, *this);
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

    if (oldVrf)
        oldVrf->getInterfaceManager().remove(configs.key);
    routingInstance.store(vrf, std::memory_order_release);
    vrf->getInterfaceManager().add(this, configs.key);

    if (!isShutdown) shutdown(false);

    return true;
}
} // namespace interface
