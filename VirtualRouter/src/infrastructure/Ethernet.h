// Ethernet.h

#ifndef ETHERNET_H
#define ETHERNET_H

#include <ByteString.hpp>
#include <PacketStructure.h>
#include <Logger.h>

class Interface;

namespace Protocol
{
    class Ethernet
    {
    public:

        // Constructs the Ethernet header in PacketInfo based on destination IP
        // Returns true of Ethernet Header was successfully set
        static bool build(Interface* iface, PacketInfo& packetInfo, const ByteString* destIp, ByteString const* destMac, ByteString type);

    private:
        // Helper method to determin if IP is multicast
        static bool isMulticast(const ByteString& ip);

        // Helper method to determine if IP is multicast
        static ByteString deriveMulticastMac(const ByteString& ip);

        // Helper method to get MAC address from ARP or handle resolution
        static ByteString getDestinationMac(Interface* iface, const ByteString& destIp, PacketInfo& packet, ByteString& type);
    };
}

#endif // ETHERNET_H
