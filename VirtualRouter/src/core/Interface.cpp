#include <Interface.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <Dhcp.h>
#include <Dhcpv6.h>
#include <Arp.h>
#include <Ndp.h>
#include <Eigrp.h>
#include <Ethernet.h>
#include <IPPacket.h>
#include <Decapsulation.h>
#include <Encapsulation.h>
#include <VirtualRouter.h>
#include <InterfaceConfigs.h>
#include <PacketBuilder.hpp>

Interface::Interface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, VirtualRouter& vrf, bool debug)
    : packetOutQueue(outQueSiz),
    routingInstance(&vrf),
    configs(vrf.global.timeManager, interfaceType, interfaceId, Functions::hexToByte(mac)),
    debug(debug),
    packetCapture(outInterface, "FF000000", inQueSiz),
    packetSend(outInterface),
    threadsRunning(false)
{
    // Set member variables
    outInt = outInterface;
    inQsiz = inQueSiz;
    outQsiz = outQueSiz;

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
        dhcpv6Server->removeInterface(this);
    }
    
    if (dhcp) delete dhcp;

    // Remove interface from list
    if (routingInstance)
    {
        routingInstance->removeInterface(configs.key);
    }
    routingInstance->global.removeInterface(configs.key);
}

void Interface::setIPv4(const uint8_t* ip, uint8_t subnet)
{
    {
        {
            std::lock_guard<std::mutex> lock(threadsRunningMutex);
            {
                std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);

                std::memcpy(configs.ipv4.ipAddress, ip, 4);
                configs.ipv4.mask = subnet;
            }
        }
        // Send gratuitous arps
        if (arp)
        {
            arp->sendReply(Variable::Mac::broadcast, ip);
            arp->sendReply(Variable::Mac::broadcast, ip);
        }
        stateChange(StateChange::IPCHANGE);
    }
}

void Interface::setIPv6(const uint8_t* ip, bool localLink, uint8_t prefix, bool eui64)
{
    InterfaceConfigs::IPv6State::IPv6Address* ipv6 = nullptr;

    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);

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
    std::lock_guard<std::mutex> lock(threadsRunningMutex);
    {
        configs.ipv4.address.store(0, std::memory_order_release);
        configs.ipv4.mask.store(0, std::memory_order_release);
        stateChange(StateChange::IPREMOVAL);
    }
    
}

void Interface::removeIPv6(const uint8_t* ip, bool linkLocal)
{
    std::lock_guard<std::mutex> lock(threadsRunningMutex);
    {
        std::unique_lock<std::shared_mutex> ipLock(configs.ipMutex);
        configs.ipv6.removeAddress(ip, linkLocal);
        stateChangeV6(StateChange::IPREMOVAL);
    }
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
    {
        std::lock_guard<std::mutex> lock(packetOutQueueMutex);
        packetOutQueue.enqueue(packetInfo.getBuffer());
    }

    packetOutQueueCV.notify_one();
}

void Interface::packetIngress() 
{
    while (threadsRunning) {
        if (packetCapture.startCapture(NULL) != 0) {
            std::cerr << "Error starting packet capture." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        }
    }
}

void Interface::packetEgress() 
{
    while (threadsRunning) { 
        ByteString packet;
        {
            std::unique_lock<std::mutex> lock(packetOutQueueMutex);
            packetOutQueueCV.wait(lock, [this]() {return !packetOutQueue.isEmpty() || !threadsRunning; });

            if (!threadsRunning && packetOutQueue.isEmpty())
            {
                break;
            }

            if (!packetOutQueue.isEmpty())
            {
                packet = packetOutQueue.dequeue();
            }
        }

        if (!packet.empty())
        {
            // Enqueue the send task to the thread pool
            routingInstance->global.threadPool.enqueue([this, packet]() {
                this->packetSend.sendPacket(packet);
            });
        }
    }
}

void Interface::process() 
{
    while (threadsRunning) {
        ByteString packet;
        {
            std::lock_guard<std::mutex> lock(packetInQueueMutex);
            if (!packetCapture.packetQueue.isEmpty()) {
                packet = packetCapture.packetQueue.dequeue().toString();
            }
        }
        if (!packet.empty() && packet.substr(0, 1) != "\xca") {
            // Enqueue the packet processing task to the thread pool
            routingInstance->global.threadPool.enqueue([this, packet]() {
                ByteString newPacket = packet;
                Packet* p = new Packet(newPacket, debug, *this);
                p->decapsulate();
                ProcessPacket process(p->packetInfo, routingInstance, this);
            });
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

void Interface::startThreads() 
{
    if (routingInstance->global.routingEnabled)
    {
        // Initialize shared pointers for Protocol objects
        if (!arp)
            arp = new Protocol::Arp(*this);
        if (!ndp)
            ndp = new Protocol::Ndp(*this);

        threadsRunning = true;

        std::lock_guard<std::mutex> lock(threadsRunningMutex); 
        // Start threads for packet ingress, egress, and processing
        thread1 = std::thread(&Interface::packetIngress, this);
        thread2 = std::thread(&Interface::packetEgress, this);
        thread3 = std::thread(&Interface::process, this);
    }
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
        
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex); 
        threadsRunning.store(false, std::memory_order_release); 
    }
    packetOutQueueCV.notify_one();
    if (thread1.joinable()) thread1.detach(); 
    if (thread2.joinable()) thread2.join(); 
    if (thread3.joinable()) thread3.join(); 
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
                eigrpPtr->ipv4->updateInterfaceList();
            }
        };
    }
    // Other updates...

    switch (state)
    {
        case StateChange::INITIATE:
        {
            if (dhcp) dhcp->initializeDhcp();
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
                eigrpPtr->ipv6->updateInterfaceList();
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
        EigrpConfigs::InterfaceConfigs* config = new EigrpConfigs::InterfaceConfigs(configs.interfaceType, configs.id);
        configs.eigrp.eigrpInterfaceConfigList[key] = config;
    }
    return configs.eigrp.eigrpInterfaceConfigList[key];
}
