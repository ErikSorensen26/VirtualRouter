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
  : configs(cfgs.vrf.getGlobal().timeManager, cfgs.interfaceType, cfgs.interfaceId, cfgs.info),
    arp(*this),
    ndp(*this),
    routingInstance(&cfgs.vrf),
    debug(cfgs.debug),
    threadsRunning(false)
{
    cfgs.vrf.getGlobal().txMgr.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
    cfgs.vrf.getGlobal().rxMgr.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
    cfgs.vrf.getGlobal().engine.hwManager->registerInterface(&configs.hwInfo, this);
}

Interface::~Interface()
{
    cleanupInterface();
    core::VirtualRouter* vrf = getVRF();
    vrf->getGlobal().txMgr.removeInterface(*this);
    vrf->getGlobal().rxMgr.removeInterface(*this);
    vrf->getGlobal().engine.hwManager->unregisterInterface(&configs.hwInfo, this);
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
            arp.sendReply(utils::readU48(ETHERNET_MAC_BROADCAST), v4addr);
            arp.sendReply(utils::readU48(ETHERNET_MAC_BROADCAST), v4addr);
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
        getVRF()->getInterfaceManager().notify(IPv6Event::IPV6_LL_READY, *this, addr);
    else
        getVRF()->getInterfaceManager().notify(IPv6Event::IPV6_READY, *this, addr);
}

void Interface::removeIPv4(const types::IPv4Prefix* prefix)
{
    if (!prefix)
    {
        types::IPv4Prefix primary = configs.ipv4.getPrimaryPrefix();
        configs.ipv4.removePrimaryAddress();
        stateChangeV4(IPv4Event::IPV4_DEL, primary);
    }
    else
    {
        configs.ipv4.removeSecondaryAddress(*prefix);
        stateChangeV4(IPv4Event::IPV4_DEL, *prefix);
    }
}

void Interface::removeAllIPv4()
{
    // TODO
}

void Interface::removeIPv6(const types::IPv6Prefix* prefix)
{
    if (prefix)
    {
        configs.ipv6.removeAddress(*prefix);
        stateChangeV6(IPv6Event::IPV6_DEL, *prefix);
    }
    else
    {
        types::IPv6Prefix ll = configs.ipv6.getLocalPrefix();
        configs.ipv6.removeLocalAddress();
        stateChangeV6(IPv6Event::IPV6_LL_DEL, ll);
    }
}

void Interface::removeAllIPv6()
{
    // TODO
    //stateChangeV6(StateChange::IPREMOVAL);
}

std::vector<std::array<uint8_t, 16>> Interface::getTentativeAddress()
{
    std::vector<std::array<uint8_t, 16>> tentative;
    std::lock_guard<std::shared_mutex> lock(configs.ipMutex);

    // Link-local (there can only be one)
    if (!configs.ipv6.linkLocalAddress->valid && configs.ipv6.linkLocalAddress->tentative)
    {
        tentative.emplace_back();
        utils::writeU128(tentative.back().data(), configs.ipv6.linkLocalAddress->prefix.addr);
    }

    // core::Global unicast
    for (const auto& addr : configs.ipv6.globalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            utils::writeU128(tentative.back().data(), addr->prefix.addr);
        }
    }

    // Unique local
    for (const auto& addr : configs.ipv6.uniqueLocalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            utils::writeU128(tentative.back().data(), addr->prefix.addr);
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
        auto markInvalid = [&](std::vector<InterfaceConfigs::IPv6State::IPv6Address*>& list) {
            for (auto it = list.begin(); it != list.end(); ++it)
            {
                if ((*it)->prefix.addr == address.addr)
                {
                    list.erase(it);
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
        if (dhcp) dhcp->shutdown();
        arp.shutdown();
        if (getVRF()->global.isIPv6UnicastRouting())
        {
            // DHCPV6
            ndp.shutdown();
        }

        getVRF()->getInterfaceManager().notify(StateChange::IF_DOWN, *this);
    }
    else if (!shut) 
    {
        if (dhcp) dhcp->initiate();
        arp.initiateArp();
        if (getVRF()->global.isIPv6UnicastRouting())
        {
            // DHCPV6
            ndp.initializeNdp();
        }

        getVRF()->getInterfaceManager().notify(StateChange::IF_READY, *this);
    }
}

void Interface::reset()
{

    arp.initiateArp();
    if (getVRF()->global.isIPv6UnicastRouting())
        ndp.initializeNdp();
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

    utils::writeU48(packetInfo.getBuffer(), mac);

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
    processing::PacketInfo packetInfo;
    processing::inspect(packetInfo, packet, size);
    processing::decapsulate(packetInfo, packet, size);
    processing::processPacket(packet, size, packetInfo, routingInstance, this);
}

void Interface::startThreads() 
{
    // Add the interface to the TX Queue manager
    core::VirtualRouter* vrf = getVRF();
    vrf->getGlobal().txMgr.start(this);
    vrf->getGlobal().rxMgr.start(this);
    vrf->getGlobal().engine.hwManager->bringUp(configs.hwInfo.ifname);

    threadsRunning = true;

    //ingress->start();
    //TODO
}

void Interface::stopThreads() 
{
    // Add the interface to the TX Queue manager
    core::VirtualRouter* vrf = getVRF();
    vrf->getGlobal().txMgr.stop(this);
    vrf->getGlobal().rxMgr.stop(this);
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

config::Reference<config::EigrpInterfaceRegistry> Interface::getEigrpConfig(uint32_t as)
{
    auto it = configs.eigrp.eigrpIfaceConfigs.find(as);
    if (it == configs.eigrp.eigrpIfaceConfigs.end())
    {
        auto* vrf = routingInstance.load(std::memory_order_relaxed);
        auto [ins, ok] = configs.eigrp.eigrpIfaceConfigs.emplace(as, vrf->getRegistry().create<config::EigrpInterfaceRegistry>());
        return ins->second;
    }
    return it->second;
}

config::Reference<config::OspfInterfaceBaseRegistry> Interface::getOspfConfig()
{
    if (!configs.ospf.ospfInterfaceConfigs.has_value())
    {
        auto* vrf = routingInstance.load(std::memory_order_relaxed);
        configs.ospf.ospfInterfaceConfigs.emplace(vrf->getRegistry().create<config::OspfInterfaceBaseRegistry>());
        vrf->getRegistry().emplace(configs.ospf.ospfInterfaceConfigs.value()->get<config::OspfInterfaceBase::BASE>());
    }
    return configs.ospf.ospfInterfaceConfigs.value();
}

} // namespace interface
