#include <Ethernet.h>
#include <Functions.h>

namespace Protocol
{
    Ethernet::Ethernet(Interface& iface, std::shared_ptr<Arp> arpHandler, ByteString mac)
        : currentInterface(iface), arp(arpHandler), macAddress(mac) {}

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
            multicastMac = {0x01, 0x00, 0x5E, (unsigned char)ipString[1], (unsigned char)ipString[2], (unsigned char)ipString[3]};
        }
        // IPv6 multicast MAC: 33:33:xx:xx:xx:xx (last 32 bits of IPv6 address)
        else if (ip.size() == 16)
        {
            multicastMac = {0x33, 0x33, (unsigned char)ipString[12], (unsigned char)ipString[13], (unsigned char)ipString[14], (unsigned char)ipString[15]};
        }
        
        return multicastMac;
    }

    ByteString Ethernet::getDestinationMac(const ByteString& destIp, PacketInfo& packet, ByteString& type)
    {
        if (isMulticast(destIp))
        {
            return deriveMulticastMac(destIp);
        }
        else
        {
            if (arp->isMacKnown(destIp))
            {
                return arp->getMac(destIp);
            }
            else
            {
                EthernetHeader eth;
                eth.destinationMac = "";
                eth.sourceMac = macAddress;
                eth.type = type;
                packet.Layer2.push_back(eth);
                arp->resolveAndSend(destIp, packet);
                Logger::getInstance().info() << "Initiated ARP resolution for IP " << destIp.toHex() << std::endl;
                return ByteString(""); // Empty MAC signifies that the packet will be sent after ARP resolution
            }
        }
    }

    bool Ethernet::setEthernetHeader(PacketInfo& packetInfo, const ByteString& destIp, ByteString type)
    {
        // Create Header
        EthernetHeader ethernetHeader;

        // Set type and destination
        ethernetHeader.type = type;
        ethernetHeader.sourceMac = macAddress;

        std::string destMac = getDestinationMac(destIp, packetInfo, type).toString();

        if (destMac.empty())
        {
            // MAC resolution is pending; the packet will be sent after
            Logger::getInstance().warn() << "Destination MAC unknown for IP " << destIp.toHex()
                                         << ". Packet will be sent after ARP resolution." << std::endl;
            return false; // Indicate that Ethernet header was not set
        }
        // Set the destination MAC
        ethernetHeader.destinationMac = destMac;

        packetInfo.Layer2.emplace_back(ethernetHeader);
        
        currentInterface.enqueuePacket(packetInfo);

        return true;
    }
}
