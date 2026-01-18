#include <Interface.h>
#include <iostream>
#include <mutex>
#include <DhcpClient.h>
//#include <Dhcpv6.h>
#include <Arp.h>
#include <Ndp.h>
#include <Ethernet.h>
#include <IPPacket.h>
#include <Decapsulation.h>
#include <Encapsulation.h>
#include <VirtualRouter.h>
#include <InterfaceConfigs.h>
#include <PacketBuilder.hpp>
#include <TxQueueManager.h>
#include <RxQueueManager.h>
#include <Global.h>
#include <Process.h>
#include <HardwareManager.h>

#include <PacketBuilder.hpp>
#include <StaticHeader.hpp>

Interface::Interface(const InterfaceCreation& cfgs)
  : configs(cfgs.vrf.global.timeManager, cfgs.interfaceType, cfgs.interfaceId, cfgs.info),
    routingInstance(&cfgs.vrf),
    debug(cfgs.debug),
    threadsRunning(false)
{
    cfgs.vrf.global.txMgr.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
    cfgs.vrf.global.rxMgr.addInterface(*this, configs.hwInfo.ifname, { .maxQueues = 1 });
    cfgs.vrf.global.engine.hwManager->registerInterface(&configs.hwInfo, this);
}

Interface::~Interface()
{
    cleanupInterface();
    VirtualRouter* vrf = getVRF();
    vrf->global.txMgr.removeInterface(*this);
    vrf->global.rxMgr.removeInterface(*this);
    vrf->global.engine.hwManager->registerInterface(&configs.hwInfo, this);
}

void Interface::cleanupInterface()
{
    shutdown(true);
    VirtualRouter* vrf = getVRF();
    if (auto dhcpv6Server = vrf->global.dhcpv6Server)
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

void Interface::setIPv4(uint32_t ip, uint8_t subnet, bool secondary)
{
    if (!secondary)
    {
        configs.ipv4.setPrimaryAddress(ip, subnet);
        configs.ipv4.mask = subnet;
        // Send gratuitous arps
        if (arp)
        {
            uint8_t addr[4];
            writeU32(addr, ip);
            arp->sendReply(ETHERNET_MAC_BROADCAST, addr);
            arp->sendReply(ETHERNET_MAC_BROADCAST, addr);
        }
        stateChange(StateChange::IPCHANGE);
    }
    else
    {
        configs.ipv4.addSecondaryAddress(ip, subnet);
        stateChange(StateChange::IPCHANGE2);
    }
}

void Interface::setIPv6(const uint8_t* ip, bool localLink, uint8_t prefix, bool eui64)
{
    InterfaceConfigs::IPv6State::IPv6Address* ipv6 = nullptr;

    {
        if (localLink)
        {
            ipv6 = configs.ipv6.addAddress(ip, true, prefix);
        }
        else if (ip[0] == 0xFC && ip[1] == 0x00)
        {
            ipv6 = configs.ipv6.addUniqueLocalAddress(ip, prefix);
        }
        else
        {
            ipv6 = configs.ipv6.addAddress(ip, false, prefix);
        }
    }

    // Run Duplicate Address Detection (dad) using NDP
    if (ipv6)
    {
        ndp->duplicateAddressDetection(ipv6, localLink);
    }
    else 
    {
        //TODO duplicate address error
        return;
    }

    if (localLink)
        stateChangeV6(StateChange::IPCHANGE);
    else
        stateChangeV6(StateChange::IPCHANGE2);
}

void Interface::removeIPv4(const IPv4Prefix* prefix)
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

void Interface::removeIPv6(const IPv6Prefix* prefix)
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
        std::copy(configs.ipv6.linkLocalAddress->ip, configs.ipv6.linkLocalAddress->ip + 16, tentative.back().begin());
    }

    // Global unicast
    for (const auto& addr : configs.ipv6.globalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            std::copy(addr->ip, addr->ip + 16, tentative.back().begin());
        }
    }

    // Unique local
    for (const auto& addr : configs.ipv6.uniqueLocalAddresses)
    {
        if (addr->tentative)
        {
            tentative.emplace_back();
            std::copy(addr->ip, addr->ip + 16, tentative.back().begin());
        }
    }

    return tentative;
}

void Interface::markAddressDuplicate(const uint8_t* addr, bool localLink)
{
    std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);

    if (localLink && std::memcmp(configs.ipv6.linkLocalAddress->ip, addr, 16) == 0)
    {
        std::fill(configs.ipv6.linkLocalAddress->ip, configs.ipv6.linkLocalAddress->ip + 16, 0);
        configs.ipv6.linkLocalAddress->valid = false;
    }
    else
    {
        auto markInvalid = [&](std::vector<InterfaceConfigs::IPv6State::IPv6Address*>& list) {
            for (auto it = list.begin(); it != list.end(); ++it)
            {
                if ((*it)->ip == addr)
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

void Interface::enqueuePacket(PacketBuilder& packetInfo, const uint8_t* mac)
{
    if (!threadsRunning.load(std::memory_order_relaxed)) return;

    if (!encapsulate(packetInfo))
    {
        Logger::getInstance().error() << "Invalid Packet" << std::endl;
        return;
    }

    if (mac)
    {
        std::memcpy(packetInfo.getBuffer(), mac, 6);
    }

    // Enqueue the serialized packet for sending
    if (packetInfo.frame.slot)
    {
        tx->push(packetInfo.frame.slot);
        //packetOutQueue.enqueue(packetInfo.slot);
    }
}

void Interface::processIngress(uint8_t* packet, size_t size) 
{
    PacketInfo packetInfo;
    inspect(packetInfo, packet, size);
    decapsulate(packetInfo, packet, size);
    processPacket(packet, size, packetInfo, routingInstance, this);
}

void Interface::startThreads() 
{
    // Add the interface to the TX Queue manager
    VirtualRouter* vrf = getVRF();
    vrf->global.txMgr.start(this);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    vrf->global.rxMgr.start(this);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    vrf->global.engine.hwManager->bringUp(configs.hwInfo.ifname);

    // Initialize shared pointers for Protocol objects
    if (!arp)
        arp = new Protocol::Arp(*this);
    if (!ndp)
        ndp = new Protocol::Ndp(*this);

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
    VirtualRouter* vrf = getVRF();
    vrf->global.txMgr.stop(this);
    vrf->global.rxMgr.stop(this);
    threadsRunning.store(false, std::memory_order_release); 
}

void Interface::stateChange(StateChange state)
{
    // Eigrp Updates
    VirtualRouter* vrf = getVRF();
    if (vrf)
    {
        std::shared_lock<std::shared_mutex> lock(vrf->eigrpAutonomousSystemMutex);
        for (const auto& [_, eigrpPtr] : vrf->eigrpList)
        {
            if (eigrpPtr->ipv4)
            {
                eigrpPtr->ipv4->refreshInterfaceList();
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
    VirtualRouter* vrf = getVRF();
    if (routingInstance)
    {
        std::shared_lock<std::shared_mutex> lock(vrf->eigrpAutonomousSystemMutex);
        for (const auto& [_, eigrpPtr] : vrf->eigrpList)
        {
            if (eigrpPtr->ipv6)
            {
                eigrpPtr->ipv6->refreshInterfaceList();
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
                        ndp->duplicateAddressDetection(addr, false);
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

VirtualRouter* Interface::getVRF()
{
    return routingInstance.load(std::memory_order_relaxed);
}

bool Interface::setVRF(VirtualRouter* vrf)
{
    VirtualRouter* oldVrf = getVRF();
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

EigrpConfigs::InterfaceConfigs* Interface::getEigrpConfig(uint32_t as, AddressFamily af, bool negate)
{
    std::pair<uint32_t, AddressFamily> key = {as, af};
    if (!configs.eigrp.eigrpInterfaceConfigList.contains(key))
    {
        if (negate) return nullptr;
        EigrpConfigs::InterfaceConfigs* config = new EigrpConfigs::InterfaceConfigs(configs.key);
        configs.eigrp.eigrpInterfaceConfigList[key] = config;
    }
    return configs.eigrp.eigrpInterfaceConfigList[key];
}
