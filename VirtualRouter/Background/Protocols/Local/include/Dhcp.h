#pragma once

#include <string>
#include <Interface.h>
#include <PacketStructure.h>
#include <Functions.h>
#include <random>
#include <mutex>
#include <thread>
#include <chrono>
#include <time.h>

double secondsSinceEpoch(); // Function to get the current time in seconds since the Unix epoch

using namespace std;

// Declares interface class
class Interface;

namespace Protocol {

// Class representing a DHCP client that handles DHCP operations
class DhcpClient {
public:
    // Constructor that initializes the DHCP client with a reference to the current interface
    DhcpClient(Interface& CurrentInterface);

    // Creates a DHCP packet body with the given hostname, hardware address, and length
    PacketInfo DhcpBody(string& hostname, string& hardwareAddress, int length);

    // Creates a DHCP Discover packet with the provided hostname and hardware address
    PacketInfo DhcpDiscover(PacketInfo packet, string& hostname, string& hardwareAddress);

    // Creates a DHCP Request packet with the given header, hostname, hardware address,
    // requested IP, and server ID
    PacketInfo DhcpRequest(PacketInfo packet, dhcpHeader& header, string& hostname, 
                           string& hardwareAddress, string& requestedIP, string& serverID);

    // Initializes the DHCP client with the given hostname and hardware address
    void InitializeDhcp(string& hostname, string& hardwareAddress);

    // Extracts DHCP options from the provided list of options
    void ExtractOptions(vector<dhcpHeader::Option> options);

    // Processes a DHCP packet with the given header and type
    void DhcpPacket(const dhcpHeader* header, string& type);

    // Generates a unique DHCP transaction ID
    std::string generateDhcpTransid();

    // Mutex for synchronizing access to DHCP-related resources
    mutex dhcpMutex;

    // Packet info for DHCP Offer and DHCP Ack
    PacketInfo dhcpOffer{};
    PacketInfo dhcpAck{};

    // Flags indicating if an offer has been received and if acknowledgment has been received
    bool offered = false;
    bool acked = false;

    // Time when the lease started, measured in seconds since epoch
    double leaseStart{};

private:
    // Pointer to the interface associated with this DHCP client
    Interface* currentInterface;

    // Variable for storing additional DHCP-related data
    Variable variable;

    // Pointer to the singleton instance of Functions
    Functions* function = Functions::getInstance();
};

}
