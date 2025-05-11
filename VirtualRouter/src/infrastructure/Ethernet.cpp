#include <Ethernet.h>
#include <Functions.h>
#include <Interface.h>
#include <Arp.h>
#include <Ndp.h>

namespace Protocol
{
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

    ByteString Ethernet::getDestinationMac(Interface* iface, const ByteString& destIp, PacketInfo& packet, ByteString& type)
    {
        if (isMulticast(destIp))
        {
            return deriveMulticastMac(destIp);
        }
        else if (destIp.size() == 4 && iface->arp)
        {
            auto* mac = iface->arp->getMac(destIp);
            if (mac)
            {
                return *mac;
            }
            else
            {
                EthernetHeader eth;
                eth.destinationMac = ByteString("");
                eth.sourceMac = iface->configs.macAddress;
                eth.type = type;
                packet.Layer2.push_back(eth);
                iface->arp->resolveAndSend(destIp, packet);
                return {}; // Empty MAC signifies that the packet will be sent after ARP resolution
            }
        }
        else if (destIp.size() == 16 && iface->ndp)
        {
            auto* mac = iface->ndp->getMac(destIp);
            if (mac)
            {
                return *mac;
            }
            else
            {
                EthernetHeader eth;
                eth.destinationMac = ByteString("");
                eth.sourceMac = iface->configs.macAddress;
                eth.type = type;
                packet.Layer2.push_back(std::move(eth));
                iface->ndp->resolveAndSend(destIp, packet);
                return {}; // Empty MAC signifies that the packet will be sent after NDP resolution
            }
        }
        else return ByteString(6, 0x00); // Resolution is disabled.
    }

    bool Ethernet::build(Interface* iface, PacketInfo& packetInfo, const ByteString* destIp, ByteString const* destMac, ByteString type)
    {
        // Create Header
        EthernetHeader ethernetHeader;

        // Set type and destination
        ethernetHeader.type = type;
        ethernetHeader.sourceMac = iface->configs.macAddress;

        ByteString destinationMac;
        if ((!destMac || destMac->size() != 6) && destIp)
        {
            destinationMac = getDestinationMac(iface, *destIp, packetInfo, type);
        }
        else
        {
            destinationMac = *destMac;
        }

        if (destinationMac.empty())
            return false; // Indicate that Ethernet header was not set

        // Set the destination MAC
        ethernetHeader.destinationMac = destinationMac;

        packetInfo.Layer2.push_back(std::move(ethernetHeader));
        
        iface->enqueuePacket(packetInfo);

        return true;
    }
}
