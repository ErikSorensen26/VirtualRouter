// Dhcp.h

#ifndef DHCP_H
#define DHCP_H

#include <string>
#include <PacketStructure.h>
#include <Functions.h>
#include <Global.h>
#include <mutex>

double secondsSinceEpoch(); // Function to get the current time in seconds since the Unix epoch

// Declares interface class
class Interface;

namespace Protocol {

    // Class representing a DHCP client that handles DHCP operations
    class DhcpClient {
    public:
        // Constructor that initializes the DHCP client with a reference to the current interface
        DhcpClient(Interface& CurrentInterface);

        // Creates a DHCP packet body with the given hostname, hardware address, and length
        PacketInfo dhcpBody(ByteString& hardwareAddress);

        // Creates a DHCP Discover packet with the provided hostname and hardware address
        PacketInfo dhcpDiscover(PacketInfo packet, std::string& hostname, ByteString& hardwareAddress);

        // Creates a DHCP Request packet with the given header, hostname, hardware address,
        // requested IP, and server ID
        PacketInfo dhcpRequest(PacketInfo packet, DhcpHeader& header, std::string& hostname, 
                               ByteString hardwareAddress, ByteString requestedIP, ByteString serverID);

        // Initializes the DHCP client with the given hostname and hardware address
        void InitializeDhcp(ByteString& hardwareAddress);

        // Extracts DHCP options from the provided list of options
        void ExtractOptions(std::vector<DhcpHeader::Option> options);

        // Processes a DHCP packet with the given header and type
        void DhcpPacket(const DhcpHeader* header, ByteString& type);

        // Generates a unique DHCP transaction ID
        ByteString generateDhcpTransid();

        // Mutex for synchronizing access to DHCP-related resources
        std::mutex dhcpMutex;

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
    };

}

#endif // DHCP_H
