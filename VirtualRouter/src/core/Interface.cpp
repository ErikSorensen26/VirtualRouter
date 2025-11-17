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
  : routingInstance(&cfgs.vrf),
    configs(cfgs.vrf.global.timeManager, cfgs.interfaceType, cfgs.interfaceId, cfgs.info),
    debug(cfgs.debug),
    threadsRunning(false)
{
    // Set member variables
    startThreads(); // TEMPORARY: will be shutdown by default once shits working
}

Interface::~Interface()
{
    stopThreads();
    cleanupInterface();
}

void Interface::cleanupInterface()
{
    shutdownFlag.store(true, std::memory_order_release);
    stateChange(StateChange::SHUTDOWN);
    stateChangeV6(StateChange::SHUTDOWN);
    if (auto dhcpv6Server = routingInstance->global.dhcpv6Server)
    {
        //dhcpv6Server->removeInterface(this);
    }
    
    if (dhcp) delete dhcp;

    // Remove interface from list
    if (routingInstance)
    {
        routingInstance->removeInterface(configs.key);
    }
    routingInstance->global.removeInterface(configs.key);
}

/**
 * @brief Sets the IPv4 address and subnet mask for the interface.
 *
 * Updates the IPv4 configuration and sends gratuitous ARP packets to update the network.
 *
 * @param ip The IPv4 address to assign to the interface.
 * @param subnet The subnet mask for the IPv4 address.
 */
void Interface::setIPv4(uint32_t ip, uint8_t subnet)
{
    {
        configs.ipv4.setAddress(ip, subnet);
        configs.ipv4.mask = subnet;
        // Send gratuitous arps
        if (arp)
        {
            uint8_t addr[4];
            writeU32(addr, ip);
            arp->sendReply(Variable::Mac::broadcast, addr);
            arp->sendReply(Variable::Mac::broadcast, addr);
        }
        stateChange(StateChange::IPCHANGE);
    }
}

/**
 * @brief Sets the IPv6 address, subnet mask, and EUI-64 flag for interface.
 *
 * Updates the IPv6 configuration and triggers Neighbor Discovery Protocol (NDP) updates.
 *
 * @param ip The IPv6 address to assign to the interface.
 * @param linkLocal Indicates if the address is linkLocal
 * @param subnet The subnet mask for the IPv6 address, Default to 64.
 * @param eui64 Flag indicating whether to use EUI-64 for IPv6 address generation.
 */
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

    stateChangeV6(StateChange::IPCHANGE);
}

void Interface::removeIPv4()
{
    configs.ipv4.address.store(0, std::memory_order_release);
    configs.ipv4.mask.store(0, std::memory_order_release);
    stateChange(StateChange::IPREMOVAL);
}

void Interface::removeIPv6(const uint8_t* ip)
{
    ip ? configs.ipv6.removeAddress(ip) : configs.ipv6.removeLocalAddress();
    stateChangeV6(StateChange::IPREMOVAL);
}

/**
 * @brief Gathers and returns all tentative addresses on the interface.
 *
 * Helper address to return all pending IPv6 addresses.
 */
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

/**
 * @brief Marks a IPv6 address as a duplicate making it invalid.
 *
 * @param address IPv6 address being marked as a duplicate
 * @param optional param stating if its a link-local address or not.
 */
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

/**
 * @brief Shuts down or restarts the interface.
 * 
 * Toggles the running state of the interface and triggers state changes for protocols.
 *
 * @param shut Boolean flag indicating whether to shut down ('true') or restart ('false').
 */
void Interface::Shutdown(bool shut) 
{
    shutdownFlag = shut;
    if (shut) 
    {
        stopThreads();
    }
    else if (!shut) 
    {
        startThreads();
    }
    stateChange(StateChange::SHUTDOWN);
    stateChangeV6(StateChange::SHUTDOWN);
}

/**
 * @brief Enqueues a packet for sending through the interface.
 *
 * Serializes and enqueues the packet, replacing the MAC address if provided.
 *
 * @param packetInfo The packet information to be sent.
 * @param mac Optional MAC address to replace the packet's source MAC.
 */
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
    routingInstance->global.threadPool.enqueue([this, packet, size]() {
        PacketInfo packetInfo;
        inspect(packetInfo, packet, size);
        decapsulate(packetInfo, packet, size);
        processPacket(packet, size, packetInfo, routingInstance, this);
    });
}

void Interface::startThreads() 
{
    // Add the interface to the TX Queue manager
    routingInstance->global.txMgr.addInterface(*this, configs.hwInfo.iface, { .maxQueues = 1 });
    routingInstance->global.rxMgr.addInterface(*this, configs.hwInfo.iface, { .maxQueues = 1 });

    if (routingInstance->global.routingEnabled)
    {
        // Initialize shared pointers for Protocol objects
        if (!arp)
            arp = new Protocol::Arp(*this);
        if (!ndp)
            ndp = new Protocol::Ndp(*this);

        threadsRunning = true;

        //ingress->start();
        //TODO
    }
}

void Interface::stopThreads() 
{
    // Add the interface to the TX Queue manager
    routingInstance->global.txMgr.removeInterface(*this);
    routingInstance->global.rxMgr.removeInterface(*this);

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
        
    threadsRunning.store(false, std::memory_order_release); 

    //ingress->stop();
    //TODO
}

void Interface::stateChange(StateChange state)
{
    // Eigrp Updates
    if (routingInstance)
    {
        std::shared_lock<std::shared_mutex> lock(routingInstance->eigrpAutonomousSystemMutex);
        for (const auto& [_, eigrpPtr] : routingInstance->eigrpList)
        {
            if (eigrpPtr->ipv4)
            {
                eigrpPtr->ipv4->refreshInterfaceList();
            }
        };
    }
    // Other updates...

    switch (state)
    {
        case StateChange::INITIATE:
        {
            if (dhcp) dhcp->initiate();
            break;
        }
        case StateChange::SHUTDOWN:
        {
            if (dhcp) dhcp->shutdown();
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
        case StateChange::IPREMOVAL:
        {
            if (arp) arp->shutdown();
            break;
        }
    }
}

void Interface::stateChangeV6(StateChange state)
{
    // Eigrp Updates
    if (routingInstance)
    {
        std::shared_lock<std::shared_mutex> lock(routingInstance->eigrpAutonomousSystemMutex);
        for (const auto& [_, eigrpPtr] : routingInstance->eigrpList)
        {
            if (eigrpPtr->ipv6)
            {
                eigrpPtr->ipv6->refreshInterfaceList();
            }
        };
    }
    // Other updates...
    
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
                ndp->initializeNdp();
            }
            break;
        }
        case StateChange::IPREMOVAL:
        {
            if (ndp) ndp->shutdown();
            break;
        }
    }
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
