#include <Ethernet.h>
#include <Functions.h>
#include <Interface.h>
#include <Arp.h>
#include <Ndp.h>

namespace Protocol
{
    Ethernet::Ethernet(Interface& iface, Arp* arpHandler, Ndp* ndpHandler, ByteString mac)
        : currentInterface(iface), arp(arpHandler), ndp(ndpHandler), macAddress(mac) {}

    bool Ethernet::isMulticast(const ByteString& ip)
    {
        return Functions::isMulticast(ip);
    }

    ByteString Ethernet::deriveMulticastMac(const ByteString& ip)
    {
        ByteString multicastMac(6, 0);

        // IPv4 multicast MAC: 01:00:5E:xx:xx:xx (lower 23 bits of IP)
        std::string ipString = ip.toString();
        if (ip.size() == 4)
        {
            multicastMac = {0x01, 0x00, 0x5E, static_cast<unsigned char>(ipString[1]), static_cast<unsigned char>(ipString[2]), static_cast<unsigned char>(ipString[3])};
        }
        // IPv6 multicast MAC: 33:33:xx:xx:xx:xx (last 32 bits of IPv6 address)
        else if (ip.size() == 16)
        {
            multicastMac = {0x33, 0x33, static_cast<unsigned char>(ipString[12]), static_cast<unsigned char>(ipString[13]), static_cast<unsigned char>(ipString[14]), static_cast<unsigned char>(ipString[15])};
        }
        
        return multicastMac;
    }

    ByteString Ethernet::getDestinationMac(const ByteString& destIp, PacketInfo& packet, ByteString& type)
    {
        if (isMulticast(destIp))
        {
            return deriveMulticastMac(destIp);
        }
        else if (destIp.size() == 4)
        {
            auto* mac = arp->getMac(destIp);
            if (mac)
            {
                return *mac;
            }
            else
            {
                EthernetHeader eth;
                eth.destinationMac = ByteString("");
                eth.sourceMac = macAddress;
                eth.type = type;
                packet.Layer2.push_back(eth);
                arp->resolveAndSend(destIp, packet);
                Logger::getInstance().info() << "Initiated ARP resolution for IP " << destIp.toHex() << std::endl;
                return ByteString(""); // Empty MAC signifies that the packet will be sent after ARP resolution
            }
        }
        else if (destIp.size() == 16)
        {
            auto* mac = ndp->getMac(destIp);
            if (mac)
            {
                return *mac;
            }
            else
            {
                EthernetHeader eth;
                eth.destinationMac = ByteString("");
                eth.sourceMac = macAddress;
                eth.type = type;
                packet.Layer2.push_back(eth);
                ndp->resolveAndSend(destIp, packet);
                Logger::getInstance().info() << "Initiated ARP resolution for IP " << destIp.toHex() << std::endl;
                return ByteString(""); // Empty MAC signifies that the packet will be sent after NDP resolution
            }
        }
    }

    bool Ethernet::setEthernetHeader(PacketInfo& packetInfo, const ByteString& destIp, ByteString const* destMac, ByteString type)
    {
        // Create Header
        EthernetHeader ethernetHeader;

        // Set type and destination
        ethernetHeader.type = type;
        ethernetHeader.sourceMac = macAddress;

        ByteString destinationMac;
        if (!destMac || destMac->size() != 6)
        {
            destinationMac = getDestinationMac(destIp, packetInfo, type).toString();
        }
        else
        {
            destinationMac = *destMac;
        }

        if (destinationMac.empty())
        {
            // MAC resolution is pending; the packet will be sent after
            Logger::getInstance().warn() << "Destination MAC unknown for IP " << destIp.toHex()
                                         << ". Packet will be sent after ARP resolution." << std::endl;
            return false; // Indicate that Ethernet header was not set
        }
        // Set the destination MAC
        ethernetHeader.destinationMac = destinationMac;

        packetInfo.Layer2.emplace_back(ethernetHeader);
        
        currentInterface.enqueuePacket(packetInfo);

        return true;
    }
}
