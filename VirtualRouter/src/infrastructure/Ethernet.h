// Ethernet.h

#ifndef ETHERNET_H
#define ETHERNET_H

#include <PacketStructure.h>
#include <Logger.h>

class Interface;
class PacketBuilder;

namespace Protocol
{
    class Ethernet
    {
    public:

        // Constructs the Ethernet header in PacketInfo based on destination IP
        // Returns true of Ethernet Header was successfully set
        static bool build(Interface* iface, PacketBuilder& packetInfo, const uint8_t* destIp, const uint8_t* destMac, uint16_t type);

        static bool reserve(PacketBuilder& packetInfo);

    private:
        // Helper method to determine if IP is multicast
        static void deriveMulticastMac(uint8_t* mac, const uint8_t* ip, AddressFamily af);

        // Helper method to get MAC address from ARP or handle resolution
        static bool getDestinationMac(uint8_t* mac, Interface* iface, const std::span<const uint8_t>& destIp, PacketBuilder& packet, uint16_t type);
    };
}

#endif // ETHERNET_H
