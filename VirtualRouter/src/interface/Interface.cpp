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

#include "eigrp/core/Eigrp.h"
#include "ospf/OspfProcess.h"

namespace interface
{

Interface::Interface(const InterfaceCreation& cfgs)
  : configs(cfgs.vrf.getGlobal().timeManager, cfgs.interfaceType, cfgs.interfaceId, cfgs.info),
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
    if (auto dhcpv6Server = vrf->getGlobal().dhcpv6Server)
    {
        //dhcpv6Server->removeInterface(this);
    }
    
    if (dhcp) delete dhcp;

    // Remove interface from list
    if (routingInstance)
    {
        vrf->removeInterface(configs.key);
    }
}

void Interface::setIPv4(types::IPv4Prefix prefix, bool secondary)
{
    if (!secondary)
    {
        configs.ipv4.setPrimaryAddress(prefix);
        configs.ipv4.mask = prefix.prefixLength;
        // Send gratuitous arps
        if (arp)
        {
            types::IPv4Address v4addr(prefix.addr);
            arp->sendReply(utils::readU48(ETHERNET_MAC_BROADCAST), v4addr);
            arp->sendReply(utils::readU48(ETHERNET_MAC_BROADCAST), v4addr);
        }
        stateChange(StateChange::IPCHANGE);
    }
    else
    {
        configs.ipv4.addSecondaryAddress(prefix);
        stateChange(StateChange::IPCHANGE2);
    }
}

void Interface::setIPv6(const types::IPv6Prefix& addr, bool linkLocal, bool eui64)
{
    InterfaceConfigs::IPv6State::IPv6Address* ipv6 = nullptr;

    {
        if (linkLocal)
        {
            ipv6 = configs.ipv6.addAddress(addr, true);
        }
        else if ((addr.addr >> 120) == 0xFC)
        {
            ipv6 = configs.ipv6.addUniqueLocalAddress(addr);
        }
        else
        {
            ipv6 = configs.ipv6.addAddress(addr, false);
        }
    }

    // Run Duplicate Address Detection (dad) using NDP
    if (ipv6)
    {
        ndp->duplicateAddressDetection(*ipv6, linkLocal);
    }
    else 
    {
        //TODO duplicate address error
        return;
    }

    if (linkLocal)
        stateChangeV6(StateChange::IPCHANGE);
    else
        stateChangeV6(StateChange::IPCHANGE2);
}

void Interface::removeIPv4(const types::IPv4Prefix* prefix)
{
    if (!prefix)
    {
        configs.ipv4.removePrimaryAddress();
        stateChange(StateChange::IPREMOVAL);
    }
    else
    {
        configs.ipv4.removeSecondaryAddress(*prefix);
    }
}

void Interface::removeIPv6(const types::IPv6Prefix* prefix)
{
    if (prefix)
    {
        configs.ipv6.removeAddress(*prefix);
        stateChangeV6(StateChange::IPREMOVAL);
    }
    else
    {
        configs.ipv6.removeLocalAddress();
        stateChangeV6(StateChange::IPREMOVAL);
    }
}

void Interface::removeAllIPv6()
{
    configs.ipv6.removeAllAddresses();
    stateChangeV6(StateChange::IPREMOVAL);
}

std::vector<std::array<uint8_t, 16>> Interface::getTentativeAddress()
{
    std::vector<std::array<uint8_t, 16>> tentative;
    std::lock_guard<std::shared_mutex> lock(configs.ipMutex);

    // Link-local (there can only be one)
    if (!configs.ipv6.linkLocalAddress->valid && configs.ipv6.linkLocalAddress->tentative)
    {
        tentative.emplace_back();
        utils::writeU128(tentative.back().data(), configs.ipv6.linkLocalAddress->addr.addr);
    }

    // core::Global unicast
    for (const auto& addr : configs.ipv6.globalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            utils::writeU128(tentative.back().data(), addr->addr.addr);
        }
    }

    // Unique local
    for (const auto& addr : configs.ipv6.uniqueLocalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            utils::writeU128(tentative.back().data(), addr->addr.addr);
        }
    }

    return tentative;
}

void Interface::markAddressDuplicate(types::IPv6Address address, bool linkLocal)
{
    std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);

    if (linkLocal && configs.ipv6.linkLocalAddress->addr.addr == address.addr)
    {
        configs.ipv6.linkLocalAddress->addr = {};
        configs.ipv6.linkLocalAddress->valid = false;
    }
    else
    {
        auto markInvalid = [&](std::vector<InterfaceConfigs::IPv6State::IPv6Address*>& list) {
            for (auto it = list.begin(); it != list.end(); ++it)
            {
                if ((*it)->addr.addr == address.addr)
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
        stateChange(StateChange::SHUTDOWN);
        stateChangeV6(StateChange::SHUTDOWN);
        stopThreads();
    }
    else if (!shut) 
    {
        startThreads();
        stateChange(StateChange::INITIATE);
        stateChangeV6(StateChange::INITIATE);
    }
}

void Interface::physicalShutdown(bool shut)
{
    if (carrierFlag.load(std::memory_order_relaxed) == !shut) return;
    carrierFlag.store(!shut, std::memory_order_release);
    shutdown(shut);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    vrf->getGlobal().rxMgr.start(this);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    vrf->getGlobal().engine.hwManager->bringUp(configs.hwInfo.ifname);

    // Initialize shared pointers for Protocol objects
    if (!arp)
        arp = new infrastructure::Arp(*this);
    if (!ndp)
        ndp = new infrastructure::Ndp(*this);

    threadsRunning = true;

    //ingress->start();
    //TODO
}

void Interface::stopThreads() 
{
    if (arp)
    {
        delete arp;
        arp = nullptr;
    }
    if (ndp)
    {
        delete ndp;
        ndp = nullptr;
    }
        
    // Add the interface to the TX Queue manager
    core::VirtualRouter* vrf = getVRF();
    vrf->getGlobal().txMgr.stop(this);
    vrf->getGlobal().rxMgr.stop(this);
    threadsRunning.store(false, std::memory_order_release); 
}

void Interface::stateChange(StateChange state)
{
    // Eigrp Updates
    core::VirtualRouter* vrf = getVRF();
    if (vrf)
    {
        for (const auto& [_, eigrpPtr] : vrf->eigrpList)
        {
            if (eigrpPtr.ipv4)
            {
                eigrpPtr.ipv4->refreshInterfaceList();
            }
        };
    }
    // Other updates...

    if (!vrf->getGlobal().routingEnabled)
        return;

    switch (state)
    {
        case StateChange::INITIATE:
        {
            if (dhcp) dhcp->initiate();
            if (arp) arp->initiateArp();
            break;
        }
        case StateChange::SHUTDOWN:
        {
            if (dhcp) dhcp->shutdown();
            if (arp) arp->shutdown();
            break;
        }
        case StateChange::IPCHANGE:
        {
            if (arp)
            {
                arp->shutdown();
                arp->initiateArp();
            }
            break;
        }
        case StateChange::IPCHANGE2:
        {
            break;
        }
        case StateChange::IPREMOVAL:
        {
            if (arp) arp->shutdown();
            break;
        }
        case StateChange::IPREMOVAL2:
        {
            break;
        }
    }
}

void Interface::stateChangeV6(StateChange state)
{
    // Eigrp Updates
    core::VirtualRouter* vrf = getVRF();
    if (routingInstance)
    {
        for (const auto& [_, eigrpPtr] : vrf->eigrpList)
        {
            if (eigrpPtr.ipv6)
            {
                eigrpPtr.ipv6->refreshInterfaceList();
            }
        };
    }
    // Other updates...

    if (!vrf->global.routingEnabled)
        return;
    
    switch (state)
    {
        case StateChange::INITIATE:
        {
            // Ndp
            if (ndp)
            {
                for (auto& addr : configs.ipv6.globalAddresses)
                {
                    if (addr->tentative)
                        ndp->duplicateAddressDetection(*addr, false);
                }
            }
            break;
        }
        case StateChange::SHUTDOWN:
        {
            break;
        }
        case StateChange::IPCHANGE:
        {
            // Ndp
            if (ndp)
            {
                ndp->shutdown();
                if (vrf->global.routingEnabled)
                    ndp->initializeNdp();
            }
            break;
        }
        case StateChange::IPCHANGE2:
        {
            break;
        }
        case StateChange::IPREMOVAL:
        {
            if (ndp) ndp->shutdown();
            break;
        }
        case StateChange::IPREMOVAL2:
        {
            break;
        }
    }
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

    stateChange(StateChange::SHUTDOWN);
    stateChangeV6(StateChange::SHUTDOWN);

    //TODO remove ipaddress configs

    removeIPv4();
    removeAllIPv6();

    getVRF()->removeInterface(configs.key);
    routingInstance.store(vrf, std::memory_order_release);
    vrf->addInterface(this, configs.key);

    stateChange(StateChange::INITIATE);
    stateChangeV6(StateChange::INITIATE);

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
