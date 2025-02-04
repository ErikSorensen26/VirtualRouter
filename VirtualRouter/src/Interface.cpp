#include <Interface.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <Decapsulation.h>

Interface::Interface(InterfaceType interfaceType, std::string outInterface, const size_t inQueSiz, const size_t outQueSiz, std::string mac, uint8_t interfaceId, bool debug)
    : packetOutQueue(outQueSiz),
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
    configs.interfaceType = interfaceType;
    configs.id = interfaceId;

    // Initialize shared pointers for Protocol objects
    arp = new Protocol::Arp(*this);
    ethernet = new Protocol::Ethernet(*this, arp, Functions::hexToByte(mac));
    ipPacket = new Protocol::IPPacket(this);

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
    shutdownFlag = true;
    stateChange();
    stateChangeV6();
    
    delete arp;
    arp = nullptr;
    delete ipPacket;
    ipPacket = nullptr;
    delete ethernet;
    ethernet = nullptr;
    if (dhcp)
    {
        delete dhcp;
        dhcp = nullptr;
    }

    // Remove interface from list
    if (interfaceList[configs.interfaceType].find(configs.id) != interfaceList[configs.interfaceType].end())
    {
        interfaceList[configs.interfaceType].erase(configs.id);
    }
}

void Interface::setIPv4(ByteString ip, uint8_t subnet)
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        {
            std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);
            configs.ipv4.ipAddress = ip; 
            configs.ipv4.mask = subnet;
        }
        // Send gratuitous arps
        arp->sendReply(Variable::Mac::broadcast, ip);
        arp->sendReply(Variable::Mac::broadcast, ip);
        stateChange();
    }
}

void Interface::setIPv6(ByteString ip, uint8_t subnet, bool eui64)
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        {
            std::lock_guard<std::shared_mutex> ipLock(configs.ipMutex);
            configs.ipv6.ipAddress = ip; 
            configs.ipv6.mask = subnet;
        }
        // NDP
        stateChangeV6();
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
                ProcessPacket process(p->packetInfo, vrf, this);
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
    for (const auto& eigrp : eigrpList)
    {
        if (eigrp.second->ipv4)
        {
            eigrp.second->ipv4->updateInterfaceList();
            eigrp.second->ipv4->updateRoutingTableForConnected();
        }
    }
    // Other updates...
}

void Interface::stateChangeV6()
{
    // Eigrp Updates
    for (const auto& eigrp : eigrpList)
    {
        if (eigrp.second->ipv6)
        {
            eigrp.second->ipv6->updateInterfaceList();
            eigrp.second->ipv6->updateRoutingTableForConnected();
        }
    }
    // Other updates...
}

// Initialize the shared pointer to the current Interface
Interface* currentInterface;

// Map to store Interface objects by string key and integer ID
std::shared_mutex interfaceListMutex;
std::map<InterfaceType, std::map<unsigned int, Interface*>> interfaceList;
