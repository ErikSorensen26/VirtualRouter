#include <Interface.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <Dhcp.h>
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

    // Initialize shared pointers for Protocol objects
    arp = new Protocol::Arp(*this);
    ndp = new Protocol::Ndp(*this);

    // Start background threads
    //startThreads();
}   

Interface::~Interface()
{
    stopThreads();
    cleanupInterface();
}

void Interface::cleanupInterface()
{
    shutdownFlag.store(true, std::memory_order_release);
    stateChange();
    stateChangeV6();
    
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
        stateChange();
    }
}

void Interface::setIPv6(ByteString ip, bool localLink, uint8_t subnet, bool eui64)
{
    {
        {
            std::lock_guard<std::mutex> lock(threadsRunningMutex);
            {
                std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);
                if (localLink)
                {
                    configs.ipv6.setTemp(ip, true);
                    configs.ipv6.tentative = true;
                    configs.ipv6.valid = false;
                }
                else
                {
                    configs.ipv6.setTemp(ip);
                    configs.ipv6.mask = subnet;
                    configs.ipv6.globalTentative = true;
                    configs.ipv6.globalValid = false;
                }
            }
        }
        // Run Duplicate Address Detection using NDP
        ndp->duplicateAddressDetection(localLink);

        {
            std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);
            if (configs.ipv6.tentative)
            {
                std::cout << "\n%" << "Duplicate Address Detected";
            }
            else
            {
                configs.ipv6.valid = true;
            }
        }

        // Trigger NDP state update for IPv6
        stateChangeV6();
    }
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

void Interface::removeIPv6(bool linkLocal)
{
    std::lock_guard<std::mutex> lock(threadsRunningMutex);
    {
        std::unique_lock<std::shared_mutex> ipLock(configs.ipMutex);
        if (linkLocal)
        {
            configs.ipv6.ipAddress.clear();
            configs.ipv6.tempAddress.clear();
            configs.ipv6.tentative = false;
            configs.ipv6.valid = false;
        }
        else
        {
            configs.ipv6.globalIpAddress.clear();
            configs.ipv6.tempGlobalAddress.clear();
            configs.ipv6.globalTentative = false;
            configs.ipv6.globalValid = false;
        }
    }
}

std::vector<ByteString> Interface::getTentativeAddress()
{
    std::vector<ByteString> tentative;
    std::lock_guard<std::shared_mutex> lock(configs.ipMutex);
    if (configs.ipv6.tentative)
    {
        tentative.push_back(configs.ipv6.tempAddress);
    }
    if (configs.ipv6.globalTentative)
    {
        tentative.push_back(configs.ipv6.tempGlobalAddress);
    }
    return tentative;
}

void Interface::markAddressDuplicate(const ByteString& addr, bool localLink)
{
    std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);
    if (localLink && configs.ipv6.ipAddress == addr)
    {
        configs.ipv6.tentative = false;
        configs.ipv6.valid = false;
    }
    else if (configs.ipv6.globalIpAddress == addr)
    {
        configs.ipv6.globalTentative = false;
        configs.ipv6.globalValid = false;
    }
}

void Interface::Shutdown(bool shut) {
    shutdownFlag = shut;
    if (shut) 
    {
        stopThreads();
    }
    else if (!shut) 
    {
        startThreads();
    }
    stateChange();
    stateChangeV6();
}

IpInfo* Interface::Get() 
{
    if (shutdownFlag)
    {
        return nullptr;
    }
    return &configs;
}

void Interface::enqueuePacket(PacketInfo& packetInfo, ByteString mac)
{
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

void Interface::packetIngress() {
    while (threadsRunning) {
        if (packetCapture.startCapture(NULL) != 0) {
            std::cerr << "Error starting packet capture." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        }
    }
}

void Interface::packetEgress() {
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

void Interface::process() {
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

void Interface::startThreads() {
    threadsRunning = true;

    std::lock_guard<std::mutex> lock(threadsRunningMutex); 
    // Start threads for packet ingress, egress, and processing
    thread1 = std::thread(&Interface::packetIngress, this);
    thread2 = std::thread(&Interface::packetEgress, this);
    thread3 = std::thread(&Interface::process, this);
}

void Interface::stopThreads() 
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex); 
        threadsRunning = false; 
    }
    packetOutQueueCV.notify_one();
    if (thread1.joinable()) thread1.detach(); 
    if (thread2.joinable()) thread2.join(); 
    if (thread3.joinable()) thread3.join(); 

    // Shutdown the thread pool
    threadPool.shutdown();
}

void Interface::stateChange()
{
    // Eigrp Updates
    if (routingInstance)
    {
        routingInstance->forEachEigrpAutonomousSystem([](uint32_t, Protocol::EigrpAutonomousSystem* eigrp)
        {
            if (eigrp->ipv4)
            {
                eigrp->ipv4->updateInterfaceList();
                eigrp->ipv4->updateRoutingTableForConnected();
            }
        });
    }
    // Other updates...
}

void Interface::stateChangeV6()
{
    // Eigrp Updates
    if (routingInstance)
    {
        routingInstance->forEachEigrpAutonomousSystem([](uint32_t, Protocol::EigrpAutonomousSystem* eigrp)
        {
            if (eigrp->ipv6)
            {
                eigrp->ipv6->updateInterfaceList();
                eigrp->ipv6->updateRoutingTableForConnected();
            }
        });
    }
    // Other updates...
}

// Initialize the shared pointer to the current Interface
Interface* currentInterface;
