// IPPacket.cpp

#include "IPPacket.h"
#include "Ethernet.h"
#include "interface/Interface.h"
#include "packet/PacketStructure.h"
#include "processing/PacketBuilder.hpp"
#include "Arp.h"
#include "Ndp.h"

namespace Protocol::IPPacket
{
void deriveMulticastMac(uint8_t* mac, const uint8_t* ip, AddressFamily af)
{
    // IPv4 multicast MAC: 01:00:5E:xx:xx:xx (lower 23 bits of IP)
    if (af == AddressFamily::IPv4)
    {
        uint8_t tempMac[6] = {
            0x01, 0x00, 0x5E,
            static_cast<unsigned char>(ip[1] & 0x7F),
            static_cast<unsigned char>(ip[2]),
            static_cast<unsigned char>(ip[3])
        };
        std::memcpy(mac, tempMac, 6);
    }
    // IPv6 multicast MAC: 33:33:xx:xx:xx:xx (last 32 bits of IPv6 address)
    else if (af == AddressFamily::IPv6)
    {
        uint8_t tempMac[6] = {
            0x33, 0x33,
            ip[12], ip[13], ip[14], ip[15]
        };
        std::memcpy(mac, tempMac, 6);
    }
}

uint8_t* getDestinationMac(uint8_t* mac, Interface* iface, AddressFamily af, const uint8_t* destIp, PacketBuilder& packet)
{
    if (Functions::isMulticast(destIp, af))
    {
        deriveMulticastMac(mac, destIp, af);
        return mac;
    }
    else if (af == AddressFamily::IPv4 && iface->arp)
    {
        uint8_t dest[4];
        std::memcpy(dest, destIp, sizeof(uint32_t));
        if (iface->arp->getMac(mac, dest))
        {
            return mac;
        }
        else
        {
            iface->arp->resolveAndSend(dest, packet);
            return nullptr; // Empty MAC signifies that the packet will be sent after ARP resolution
        }
    }
    else if (af == AddressFamily::IPv6 && iface->ndp)
    {
        if (iface->ndp->getMac(mac, destIp))
        {
            return mac;
        }
        else
        {
            iface->ndp->resolveAndSend(destIp, packet);
            return nullptr;
        }
    }
    else return nullptr; // Resolution is disabled.
}

void reserveIpv4(PacketBuilder& packetInfo)
{
    // Decide layer 2 encapsulation based on interface configs
    // DEFAULT -> Ethernet
    Ethernet::reserve(packetInfo);
    
    size_t ipSize = IPv4Header::fixedSize;
    //TODO calcualte option lengths

    packetInfo.reserveHeader(HeaderType::IPV4, ipSize);
}

void buildIpv4(
    BuildIP& ipv4Build
)
{
    if (!ipv4Build.iface || ipv4Build.iface->shutdownFlag.load(std::memory_order_relaxed)) return;
    
    BuildEntry* nextHeader = ipv4Build.packetInfo.nextBuildHeader();
    if (!nextHeader || nextHeader->type != HeaderType::IPV4)
        return; // Drop Packet

    IPv4Header ip;

    ip.setBuffer(nextHeader->buffer); //TODO
    if (ipv4Build.sourceIp)
        ip.setSourceAddress(ipv4Build.sourceIp);
    else
        ipv4Build.iface->configs.ipv4.getPrimaryAddress(ip.raw->sourceAddress);
    ip.setVersion(4);
    ip.setHeaderLength(5); //TODO
    ip.setTypeOfService(ipv4Build.DSCP);
    ip.setTotalLength(0);
    ip.setIdentification(0);
    ip.setFlags(ipv4Build.reserved, ipv4Build.moreFragment, ipv4Build.dontFragment);
    ip.setFragmentOffset(ipv4Build.fragmentOffset);
    ip.setTtl(ipv4Build.hopLimit);
    ip.setProtocol(ipv4Build.protocolType);
    ip.setDestinationAddress(ipv4Build.destIp);

    if (!ipv4Build.destMac)
    {
        uint8_t mac[6];
        if (!getDestinationMac(mac, ipv4Build.iface, AddressFamily::IPv4, ipv4Build.destIp, ipv4Build.packetInfo))
            return; // ARP resolution in progress or failed
        Ethernet::build(ipv4Build.iface, ipv4Build.packetInfo, mac, ETHERNET_IPV4);
    }
    else
    {
        Ethernet::build(ipv4Build.iface, ipv4Build.packetInfo, ipv4Build.destMac, ETHERNET_IPV4);
    }
}

void buildIpv6(
    BuildIP& ipv6Build,
    uint32_t v6FlowLabel
)
{
    if (!ipv6Build.iface || ipv6Build.iface->shutdownFlag.load(std::memory_order_relaxed)) return;

    BuildEntry* nextHeader = ipv6Build.packetInfo.nextBuildHeader();
    if (!nextHeader || nextHeader->type != HeaderType::IPV6)
        return; // Drop Packet

    // IPv6 Header creation
    IPv6Header ip;
    ip.setBuffer(nextHeader->buffer);
    if (ipv6Build.sourceIp)
    {
        ip.setSourceAddress(ipv6Build.sourceIp);
    }
    else
    {
        // Pick best IP to use
        std::span<const uint8_t> prefix(ipv6Build.destIp, 2);

        uint8_t firstByte = ipv6Build.destIp[0];
        uint8_t secondByte = ipv6Build.destIp[1];

        // Multicast (FF00::/8)
        if (firstByte == 0xFF)
        {
            uint8_t scope = secondByte & 0x0F; // Low nibble = bits 12-15
        
            if (scope == 0x01 || scope == 0x02)
            {
                if (!ipv6Build.iface->configs.ipv6.getLocalAddress(ip.raw->sourceAddress)) return;
            }
            else if (scope == 0x05 || scope == 0x08) // Site/org-local
            {
                if (!ipv6Build.iface->configs.ipv6.getLocalUnicast(ip.raw->sourceAddress)) return;
            }
            else if (scope == 0x0E) // Global scope
            {
                if (!ipv6Build.iface->configs.ipv6.getGlobalUnicast(ip.raw->sourceAddress)) return;
            }
            else return; // Return if no matching scope is found
        }
        // Global Unicast (2000::/3 = 001xxxxx)
        else if ((firstByte & 0b11100000) == 0b00100000)
        {
            if (!ipv6Build.iface->configs.ipv6.getGlobalUnicast(ip.raw->sourceAddress)) return;
        }
        // Unique Local (FC00::/7 = 1111110x)
        else if ((firstByte & 0b11111110) == 0b11111100)
        {
            if (!ipv6Build.iface->configs.ipv6.getLocalUnicast(ip.raw->sourceAddress)) return;
        }
        // Link-local (FE80::/10 = 1111111010xxxxxxx)
        else if ((firstByte & 0b11111100) == 0b11111100 && (secondByte & 0b00001111) == 0x00)
        {
            if (!ipv6Build.iface->configs.ipv6.getLocalAddress(ip.raw->sourceAddress)) return;
        }
        // Loopback (::1)
        else if (std::all_of(ipv6Build.destIp, ipv6Build.destIp + 15, [](uint8_t b) { return b == 0; }) && ipv6Build.destIp[15] == 1)
        {
            std::memset(ip.raw->sourceAddress, 0, 16);
            ip.raw->sourceAddress[15] = 0x01;             // Set last byte to 1 using bitwise operator
        }
        else
        {
            return; // Unsuported or unconfigured destination
        }
    }
    ip.setVersionTrafficClassFlow(6, ipv6Build.DSCP, v6FlowLabel);
    ip.setNextHeader(ipv6Build.protocolType);
    ip.setHopLimit(ipv6Build.hopLimit);
    ip.setDestinationAddress(ipv6Build.destIp);

    if (!ipv6Build.destMac)
    {
        uint8_t mac[6];
        if (!getDestinationMac(mac, ipv6Build.iface, AddressFamily::IPv6, ipv6Build.destIp, ipv6Build.packetInfo))
            return; // NDP resolution in progress or failed
        Ethernet::build(ipv6Build.iface, ipv6Build.packetInfo, mac, ETHERNET_IPV6);
    }
    else
    {
        Ethernet::build(ipv6Build.iface, ipv6Build.packetInfo, ipv6Build.destMac, ETHERNET_IPV6);
    }

    if (!ipv6Build.dontFragment)
    {
        //TODO handle fragmentation
    }
}

void reserveIpv6(PacketBuilder& packetInfo)
{
    // Decide layer 2 encapsulation based on interface configs
    // DEFAULT -> Ethernet
    Ethernet::reserve(packetInfo);
    
    size_t ipSize = IPv6Header::fixedSize;
    //TODO calcualte option lengths

    packetInfo.reserveHeader(HeaderType::IPV6, ipSize);
}
} // Namespace Protocol::Namespace
