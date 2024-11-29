#include <Interface.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>
#include <memory>
#include <string>

using namespace std;

// Constructor for the Interface class
Interface::Interface(string outInterface, const int inQueSiz, const int outQueSiz, std::string mac, char interfaceId, bool debug)
    : packetCapture(outInterface, "FF000000", inQueSiz), 
      packetSend(outInterface),
      threadsRunning(false), 
      packetOutQueue(outQueSiz),
      debug(debug)
{
    // Set member variables
    outInt = outInterface;
    inQsiz = inQueSiz;
    outQsiz = outQueSiz;
    macAddress = Functions::hexToByte(mac);
    id = interfaceId;
    // Initialize shared pointers for Protocol objects
    dhcp = std::make_shared<Protocol::DhcpClient>(*this);
    arp = std::make_shared<Protocol::Arp>(*this);
    // Start background threads
    startThreads();
}

// Set IPv4 address and subnet mask
void Interface::setIPv4(string ip, string subnet)
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        ipAddress = ip; 
        mask = subnet;
        stateChange();
    }
}

void Interface::setIPv6(string ip, int subnet, bool eui64)
{
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        ipv6Address = ip;
        mask = subnet;
        stateChangeV6();
    }
}

// Get current IP address, subnet mask, MAC address, and speed information
ipInfo Interface::Get() {
    std::lock_guard<std::mutex> lock(ipInfoMutex);
    ipInfo info;
    info.id = id;
    info.bandwidth = bandwidth;
    info.delay = delay;
    info.ip = ipAddress;
    info.ipv6 = ipv6Address;
    info.subnet = mask;
    info.v6subnet = v6mask;
    info.mac = macAddress;  
    info.mtu = mtu;
    info.ipv6FlowLabel = ipv6FlowLabel;
    return info;
}

// Start background threads for packet processing
void Interface::startThreads() {
    threadsRunning = true;

    std::lock_guard<std::mutex> lock(threadsRunningMutex); 
    // Start threads for packet ingress, egress, and processing
    thread1 = std::thread(&Interface::PacketIngress, this, std::ref(packetCapture));
    thread2 = std::thread(&Interface::PacketEgress, this, std::ref(packetSend));
    thread3 = std::thread(&Interface::Process, this, std::ref(packetCapture));
}

// Function to handle packet ingress
void Interface::PacketIngress(Ingress& packetCapture) {
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
void Interface::PacketEgress(Egress& packetSend) {
    std::lock_guard<std::mutex> lock(packetOutQueueMutex);
    while (threadsRunning) { 
        {
            if (!packetOutQueue.isEmpty()) { 
                std::string packet = packetOutQueue.dequeue(); 
                packetSend.sendPacket(packet);
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
}

// Function to process packets
void Interface::Process(Ingress& packetCapture) {
    std::lock_guard<std::mutex> lock(packetInQueueMutex); 
    while (threadsRunning) {
        {
            if (!packetCapture.packetQueue.isEmpty()) {
                std::string packet = packetCapture.packetQueue.dequeue();
                Packet p(packet, debug);
                p.Decapsulate();
                PacketInfo PacketInformation = p.packetInfo;
                ProcessPacket process(PacketInformation, vrf, this, shutdown);
                packet = Encapsulate(PacketInformation, p.afterPacket);
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
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
}

// Shutdown or restart interface threads based on the shut parameter
void Interface::Shutdown(bool shut) {
    if (shut) {
        threadsRunning = false;
        shutdown = true;
    } else if (!shut) {
        threadsRunning = true;
        shutdown = false;
    }
    stateChange();
    stateChangeV6();
}

// Runs when the interface state changes
void Interface::stateChange()
{
    UpdateEigrpInterface(this);
    for (const auto& eigrp : eigrpList)
    {
        for (const auto& as : eigrp.second->autonomousSystems)
        {
            auto af = as.second->addressFamilies.find(AddressFamily::IPv4);
            if (af != as.second->addressFamilies.end())
            {
                af->second->UpdateInterfaceList();
                af->second->UpdateRoutingTableForConnected();
            }
        }
    }
}

// Runs when the interface state changes
void Interface::stateChangeV6()
{
    UpdateEigrpInterface(this);
    for (const auto& eigrp : eigrpList)
    {
        for (const auto& as : eigrp.second->autonomousSystems)
        {
            auto af = as.second->addressFamilies.find(AddressFamily::IPv6);
            if (af != as.second->addressFamilies.end())
            {
                af->second->UpdateInterfaceList();
                af->second->UpdateRoutingTableForConnected();
            }
        }
    }
}

// Destructor to ensure threads are stopped
Interface::~Interface() 
{
    stopThreads(); 
}

// Initialize the shared pointer to the current Interface
Interface* CurrentInterface{};

// Map to store Interface objects by string key and integer ID
map<string, std::map<int, std::shared_ptr<Interface>>> InterfaceList;
