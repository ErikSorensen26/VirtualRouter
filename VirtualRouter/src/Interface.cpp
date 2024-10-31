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
Interface::Interface(string outInterface, const int inQueSiz, const int outQueSiz)
    : packetCapture(outInterface, "FF000000", inQueSiz), 
      packetSend(outInterface),
      threadsRunning(false), 
      packetOutQueue(outQueSiz) 
{
    // Set member variables
    outInt = outInterface;
    inQsiz = inQueSiz;
    outQsiz = outQueSiz;
    // Initialize shared pointers for Protocol objects
    dhcp = std::make_shared<Protocol::DhcpClient>(*this);
    arp = std::make_shared<Protocol::Arp>(*this);
    // Start background threads
    startThreads();
}

// Set IPv4 address and subnet mask
void Interface::setIPv4(string ip, string subnet) {
    {
        std::lock_guard<std::mutex> lock(threadsRunningMutex);
        ipAddress = ip; 
        mask = subnet;
        string sub = function->addressToHex(subnet);
        StateChange();
    }
}

// Get current IP address, subnet mask, MAC address, and speed information
ipInfo Interface::Get() {
    ipInfo info;
    info.ip = ipAddress;
    info.subnet = mask;
    info.mac = macAddress; 
    info.speed = bandwidth; 
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
                Packet p(packet);
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
    StateChange();
}

// Runs whem the interface state changes
void Interface::StateChange()
{
    UpdateEigrpInterface(this);
    for (const auto& eigrp : eigrpList)
    {
        eigrp.second->UpdateInterfaceList();
        eigrp.second->UpdateRoutingTableForConnected();
    }
    for (const auto& eigrpInt : eigrpInterfaceList)
    {
        eigrpInt.second->eigrpProcess->OnInterfaceChange(this);
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
