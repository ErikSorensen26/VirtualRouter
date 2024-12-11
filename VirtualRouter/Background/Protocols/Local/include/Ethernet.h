#pragma once

#include <ByteString.hpp>
#include <PacketStructure.h>
#include <Arp.h>
#include <Interface.h>
#include <Logger.h>

namespace Protocol
{
    class Arp;
    class Ethernet
    {
    public:
        // Constructor takes references to Interface and Arp classes
        Ethernet(Interface& iface, std::shared_ptr<Arp> arpHandler, ByteString mac);

        // Constructs the Ethernet header in PacketInfo based on destination IP
        // Returns true of Ethernet Header was successfully set
        bool setEthernetHeader(PacketInfo& packetInfo, const ByteString& destIp, ByteString type);

    private:
        Interface& currentInterface;
        std::shared_ptr<Arp> arp;

        // Helper method to determin if IP is multicast
        bool isMulticast(const ByteString& ip);

        // Helper method to determine if IP is multicast
        ByteString deriveMulticastMac(const ByteString& ip);

        // Helper method to get MAC address from ARP or handle resolution
        ByteString getDestinationMac(const ByteString& destIp, PacketInfo& packet, ByteString& type);

        ByteString macAddress;
    };
}

