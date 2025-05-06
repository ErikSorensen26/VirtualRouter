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

Interface::Interface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, float interfaceId, VirtualRouter* vrf, bool debug)
    : packetOutQueue(outQueSiz),
      routingInstance(vrf),
      debug(debug),
      packetCapture(outInterface, "FF000000", inQueSiz),
      packetSend(outInterface),
      threadsRunning(false), 
      threadPool(1/*std::thread::hardware_concurrency()*/)
{
    // Set member variables
    outInt = outInterface;
    inQsiz = inQueSiz;
    outQsiz = outQueSiz;

    // Configs
    configs.macAddress = Functions::hexToByte(mac);
    configs.interfaceType.store(interfaceType, std::memory_order_release);
    configs.id.store(interfaceId, std::memory_order_release);

    startThreads(); // TEMPORARY: will be shutdown by default once shits working

    // Initialize shared pointers for Protocol objects
    arp = new Protocol::Arp(*this);
    ndp = new Protocol::Ndp(*this);
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
    if (Global::getInstance().dhcpServer)
    {
        Global::getInstance().dhcpv6Server->removeInterface(this);
    }
    
    if (arp) delete arp;
    arp = nullptr;
    if (ndp) delete ndp;
    ndp = nullptr;
    if (dhcp) delete dhcp;

    // Remove interface from list
    if (routingInstance)
    {
        routingInstance->removeInterface(configs.interfaceType, configs.id);
    }
    Global::getInstance().removeInterface(configs.interfaceType, configs.id);
}

void Interface::setIPv4(ByteString ip, uint8_t subnet)
{
    {
        {
            std::lock_guard<std::mutex> lock(threadsRunningMutex);
            {
                std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);
                configs.ipv4.ipAddress = ip; 
                configs.ipv4.mask = subnet;
            }
        }
        // Send gratuitous arps
        arp->sendReply(Variable::Mac::broadcast, ip);
        arp->sendReply(Variable::Mac::broadcast, ip);
        stateChange(StateChange::IPCHANGE);
    }
}

void Interface::setIPv6(ByteString ip, bool localLink, uint8_t prefix, bool eui64)
{
    IpInfo::IPv6::IPv6Address* ipv6 = nullptr;

    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);

        if (localLink)
        {
            ipv6 = configs.ipv6.addAddress(ip, true, prefix);
        }
        else if (ip.substr(0, 2) == "\xfc\x00")
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
        std::unique_lock<std::shared_mutex> ipLock(configs.ipMutex);
        configs.ipv4.ipAddress.clear();
        configs.ipv4.mask = 0;
    }
    
}

void Interface::removeIPv6(const ByteString& ip, bool linkLocal)
{
    std::lock_guard<std::mutex> lock(threadsRunningMutex);
    {
        std::unique_lock<std::shared_mutex> ipLock(configs.ipMutex);
        configs.ipv6.removeAddress(ip, linkLocal);
        stateChangeV6(StateChange::IPREMOVAL);
    }
}

std::vector<ByteString> Interface::getTentativeAddress()
{
    std::vector<ByteString> tentative;
    std::lock_guard<std::shared_mutex> lock(configs.ipMutex);

    // Link-local (there can only be one)
    if (!configs.ipv6.linkLocalAddress->ip.empty() && configs.ipv6.linkLocalAddress->tentative)
    {
        tentative.push_back(configs.ipv6.linkLocalAddress->ip);
    }

    // Global unicast
    for (const auto& addr : configs.ipv6.globalAddresses)
    {
        if (addr->tentative)
        {
            tentative.push_back(addr->ip);
        }
    }

    // Unique local
    for (const auto& addr : configs.ipv6.uniqueLocalAddresses)
    {
        if (addr->tentative)
        {
            tentative.push_back(addr->ip);
        }
    }

    return tentative;
}

void Interface::markAddressDuplicate(const ByteString& addr, bool localLink)
{
    std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);

    if (localLink && configs.ipv6.linkLocalAddress->ip == addr)
    {
        configs.ipv6.linkLocalAddress->ip.clear();
    }
    else
    {
        auto markInvalid = [&](std::vector<IpInfo::IPv6::IPv6Address*>& list) {
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

// IpInfo* Interface::Get() 
// {
//     if (shutdownFlag)
//     {
//         return nullptr;
//     }
//     return &configs;
// }

void Interface::enqueuePacket(PacketInfo& packetInfo, ByteString mac)
{
    if (!threadsRunning.load(std::memory_order_relaxed)) return;

    auto serializedPacket = encapsulate(packetInfo);
    if (!serializedPacket.has_value() || serializedPacket.value().empty())
    {
        Logger::getInstance().error() << "Invalid Packet" << std::endl;
        return;
    }

    if (!mac.empty())
    {
        serializedPacket.value().replace(0, 6, mac);
    }

    // Enqueue the serialized packet for sending
    {
        std::lock_guard<std::mutex> lock(packetOutQueueMutex);
        packetOutQueue.enqueue(serializedPacket.value());
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
            threadPool.enqueue([this, packet]() {
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
            threadPool.enqueue([this, packet]() {
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
    if (Global::getInstance().routingEnabled)
    {
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
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex); 
        threadsRunning.store(false, std::memory_order_release); 
    }
    packetOutQueueCV.notify_one();
    if (thread1.joinable()) thread1.detach(); 
    if (thread2.joinable()) thread2.join(); 
    if (thread3.joinable()) thread3.join(); 

    // Shutdown the thread pool
    threadPool.shutdown();
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

IpInfo::IPv6::IPv6Address* IpInfo::IPv6::addAddress(const ByteString& ip, bool local, uint8_t prefix)
{
    if (local)
    {
        // Only one local-address can exist
        if (linkLocalAddress->ip.empty())
        {
            linkLocalAddress->ip = ip;
            linkLocalAddress->prefix = prefix;
            linkLocalAddress->tentative = true;
            linkLocalAddress->valid = false;
            return linkLocalAddress;
        }
        else
        {
            std::cerr << "Error: Link-Local address already assigned";
        }
    }
    else
    {
        IPv6Address* address = new IPv6Address();
        address->ip = ip;
        address->prefix = prefix;
        address->tentative = true;
        address->valid = false;
        globalAddresses.push_back(address);
        return globalAddresses.back();
    }
    return nullptr;
}

IpInfo::IPv6::IPv6Address* IpInfo::IPv6::addUniqueLocalAddress(const ByteString& ip, uint8_t prefixLen)
{
    IPv6Address* address = new IPv6Address();
    address->ip = ip;
    address->prefix = prefixLen;
    address->tentative = true;
    address->valid = false;
    uniqueLocalAddresses.push_back(address);
    return uniqueLocalAddresses.back();
}
