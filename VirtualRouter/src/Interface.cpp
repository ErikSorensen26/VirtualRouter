#include <Interface.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <Decapsulation.h>

// Constructor for the Interface class
Interface::Interface(std::string outInterface, const int inQueSiz, const int outQueSiz, std::string mac, int interfaceId, bool debug)
    : packetCapture(outInterface, "FF000000", inQueSiz),
      packetSend(outInterface),
      threadsRunning(false), 
      packetOutQueue(outQueSiz),
      debug(debug),
      threadPool(std::thread::hardware_concurrency())
{
    // Set member variables
    outInt = outInterface;
    inQsiz = inQueSiz;
    outQsiz = outQueSiz;
    configs.macAddress = Functions::hexToByte(mac);
    id = interfaceId;

    // Initialize shared pointers for Protocol objects
    arp = std::make_shared<Protocol::Arp>(*this);
    ethernet = std::make_shared<Protocol::Ethernet>(*this, arp, Functions::hexToByte(mac));
    ipPacket = std::make_shared<Protocol::IPPacket>(*this);
    dhcp = std::make_shared<Protocol::DhcpClient>(*this);

    // Start background threads
    startThreads();
}   

Interface::~Interface()
{
    stopThreads();
}

// Enqueue packet
void Interface::enqueuePacket(PacketInfo& packetInfo, ByteString mac)
{
    ByteString serializedPacket = encapsulate(packetInfo);

    if (serializedPacket.empty())
    {
        Logger::getInstance().error() << "Serialized packet is empty. Aborting send." << std::endl;
        return;
    }

    if (!mac.empty())
    {
        serializedPacket.replace(0, 6, mac);
    }

    // Enqueue the serialized packet for sending
    {
        std::lock_guard<std::mutex> lock(packetOutQueueMutex);
        packetOutQueue.enqueue(serializedPacket);
    }

    packetOutQueueCV.notify_one();
}

// Set IPv4 address and subnet mask
void Interface::setIPv4(std::string ip, int subnet)
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        configs.ipAddress = ip; 
        configs.mask = subnet;
        // Send gratuitous arps
        arp->sendReply(Variable::Mac::broadcast, ip);
        arp->sendReply(Variable::Mac::broadcast, ip);
        stateChange();
    }
}

void Interface::setIPv6(std::string ip, int subnet, bool eui64)
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        configs.ipv6Address = ip;
        configs.mask = subnet;
        // NDP
        stateChangeV6();
    }
}

// Get current IP address, subnet mask, MAC address, and speed information
ipInfo Interface::Get() {
    std::lock_guard<std::mutex> lock(ipInfoMutex);
    return configs;
}

// Start background threads for packet processing
void Interface::startThreads() {
    threadsRunning = true;

    std::lock_guard<std::mutex> lock(threadsRunningMutex); 
    // Start threads for packet ingress, egress, and processing
    thread1 = std::thread(&Interface::packetIngress, this, std::ref(packetCapture));
    thread2 = std::thread(&Interface::packetEgress, this, std::ref(packetSend));
    thread3 = std::thread(&Interface::process, this, std::ref(packetCapture));
}

// Function to handle packet ingress
void Interface::packetIngress(Ingress& packetCapture) {
    while (threadsRunning) {
        if (packetCapture.startCapture(NULL) != 0) {
            std::cerr << "Error starting packet capture." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        }
    }
}

// Function to handle packet egress (sending packets)
void Interface::packetEgress(Egress& packetSend) {
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
            threadPool.enqueue([this, packet, &packetSend]() {
                packetSend.sendPacket(packet);
                Logger::getInstance().info() << "Packet sent via egress." << std::endl;
            });
        }
    }
}

// Function to process packets
void Interface::process(Ingress& packetCapture) {
    while (threadsRunning) {
        ByteString packet;
        {
            std::lock_guard<std::mutex> lock(packetInQueueMutex);
            if (!packetCapture.packetQueue.isEmpty()) {
                packet = packetCapture.packetQueue.dequeue();
            }
        }

        if (!packet.empty()) {
            // Enqueue the packet processing task to the thread pool
            threadPool.enqueue([this, packet]() {
                ByteString newPacket = packet;
                Packet p(newPacket, debug, *this);
                p.decapsulate();
                PacketInfo packetInformation = p.packetInfo;
                ProcessPacket process(packetInformation, vrf, this, shutdownFlag);
            });
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

// Stop all background threads
void Interface::stopThreads() {
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex); 
        threadsRunning = false; 
    }
    if (thread1.joinable()) thread1.detach(); 
    if (thread2.joinable()) thread2.join(); 
    if (thread3.joinable()) thread3.join(); 

    // Shutdown the thread pool
    // threadPool.shutdown();
}

// Shutdown or restart interface threads based on the shut parameter
void Interface::Shutdown(bool shut) {
    if (shut) {
        threadsRunning = false;
        shutdownFlag = true;
    } else if (!shut) {
        threadsRunning = true;
        shutdownFlag = false;
    }
    stateChange();
    stateChangeV6();
}

// Runs when the interface state changes
void Interface::stateChange()
{
    updateEigrpInterface(this);
    for (const auto& eigrp : eigrpList)
    {
        for (const auto& as : eigrp.second->autonomousSystems)
        {
            auto af = as.second->addressFamilies.find(AddressFamily::IPv4);
            if (af != as.second->addressFamilies.end())
            {
                af->second->updateInterfaceList();
                af->second->updateRoutingTableForConnected();
            }
        }
    }
}

// Runs when the interface state changes
void Interface::stateChangeV6()
{
    updateEigrpInterface(this);
    for (const auto& eigrp : eigrpList)
    {
        for (const auto& as : eigrp.second->autonomousSystems)
        {
            auto af = as.second->addressFamilies.find(AddressFamily::IPv6);
            if (af != as.second->addressFamilies.end())
            {
                af->second->updateInterfaceList();
                af->second->updateRoutingTableForConnected();
            }
        }
    }
}

// Initialize the shared pointer to the current Interface
Interface* currentInterface{};

// Map to store Interface objects by string key and integer ID
std::map<std::string, std::map<int, std::shared_ptr<Interface>>> interfaceList;
