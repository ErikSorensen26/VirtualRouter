// IPPacket.cpp

#include <IPAddress.h>

#include "IPPacket.h"
#include "Ethernet.h"
#include "interface/Interface.h"
#include "packet/PacketStructure.h"
#include "packet/headers/EthernetHeader.hpp"
#include "processing/PacketBuilder.hpp"
#include "Arp.h"
#include "Ndp.h"

namespace infrastructure::ippacket
{
static void deriveMulticastMac(uint8_t* mac, types::IPAddress ip)
{
    // IPv4 multicast MAC: 01:00:5E:xx:xx:xx (lower 23 bits of IP)
    if (ip.isIPv4())
    {
        const auto& raw = ip.v4raw(); // byte 0 = MSB of IPv4 address
        mac[0] = 0x01;
        mac[1] = 0x00;
        mac[2] = 0x5E;
        mac[3] = raw[1] & 0x7F; // second octet, strip high bit
        mac[4] = raw[2];
        mac[5] = raw[3];
    }
    // IPv6 multicast MAC: 33:33:xx:xx:xx:xx (last 32 bits of IPv6 address)
    else
    {
        const auto& raw = ip.v6raw(); // byte 0 = MSB of IPv6 address
        mac[0] = 0x33;
        mac[1] = 0x33;
        mac[2] = raw[12];
        mac[3] = raw[13];
        mac[4] = raw[14];
        mac[5] = raw[15];
    }
}

static bool getDestinationMac(uint64_t& outMac, interface::Interface* iface, const types::IPAddress& destIp, processing::PacketBuilder& packet)
{
    uint8_t macBuf[6];

    if (destIp.isIPv4())
    {
        if (destIp.isMulticast())
        {
            deriveMulticastMac(macBuf, destIp);
            outMac = utils::readU48(macBuf);
            return true;
        }
        if (iface->arp)
        {
            types::IPv4Address v4addr(destIp.v4());
            if (iface->arp->getMac(macBuf, v4addr))
            {
                outMac = utils::readU48(macBuf);
                return true;
            }
            else
            {
                iface->arp->resolveAndSend(v4addr, packet);
                return false;
            }
        }
    }
    else
    {
        if (destIp.isMulticast())
        {
            deriveMulticastMac(macBuf, destIp);
            outMac = utils::readU48(macBuf);
            return true;
        }
        if (iface->ndp)
        {
            types::IPv6Address v6addr(destIp.v6());
            if (iface->ndp->getMac(macBuf, v6addr))
            {
                outMac = utils::readU48(macBuf);
                return true;
            }
            else
            {
                iface->ndp->resolveAndSend(v6addr, packet);
                return false;
            }
        }
    }
    return false; // Resolution disabled
}

void reserveIpv4(processing::PacketBuilder& packetInfo)
{
    // Decide layer 2 encapsulation based on interface configs
    // DEFAULT -> Ethernet
    ethernet::reserve(packetInfo);

    size_t ipSize = packet::IPv4Header::fixedSize;
    //TODO calcualte option lengths

    packetInfo.reserveHeader(packet::HeaderType::IPV4, ipSize);
}

void buildIpv4(
    BuildIP& ipv4Build
)
{
    if (!ipv4Build.iface || ipv4Build.iface->shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::BuildEntry* nextHeader = ipv4Build.packetInfo.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::IPV4)
        return; // Drop Packet

    packet::IPv4Header ip;

    ip.setBuffer(nextHeader->buffer); //TODO
    if (ipv4Build.sourceIp)
    {
        ip.setSourceAddress(ipv4Build.sourceIp->v4());
    }
    else
        ip.setSourceAddress(ipv4Build.iface->configs.ipv4.getPrimaryAddress().addr);
    ip.setVersion(4);
    ip.setHeaderLength(5); //TODO
    ip.setTypeOfService(ipv4Build.DSCP);
    ip.setTotalLength(0);
    ip.setIdentification(0);
    ip.setFlags(ipv4Build.reserved, ipv4Build.moreFragment, ipv4Build.dontFragment);
    ip.setFragmentOffset(ipv4Build.fragmentOffset);
    ip.setTtl(ipv4Build.hopLimit);
    ip.setProtocol(ipv4Build.protocolType);
    ip.setDestinationAddress(ipv4Build.destIp.v4());

    if (!ipv4Build.destMac)
    {
        uint64_t mac = 0;
        if (!getDestinationMac(mac, ipv4Build.iface, ipv4Build.destIp, ipv4Build.packetInfo))
            return; // ARP resolution in progress or failed
        ethernet::build(ipv4Build.iface, ipv4Build.packetInfo, mac, ETHERNET_IPV4);
    }
    else
    {
        ethernet::build(ipv4Build.iface, ipv4Build.packetInfo, ipv4Build.destMac.value(), ETHERNET_IPV4);
    }
}

void buildIpv6(
    BuildIP& ipv6Build,
    uint32_t v6FlowLabel
)
{
    if (!ipv6Build.iface || ipv6Build.iface->shutdownFlag.load(std::memory_order_relaxed)) return;

    processing::BuildEntry* nextHeader = ipv6Build.packetInfo.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::IPV6)
        return; // Drop Packet

    // IPv6 Header creation
    packet::IPv6Header ip;
    ip.setBuffer(nextHeader->buffer);
    if (ipv6Build.sourceIp)
    {
        ip.setSourceAddress(ipv6Build.sourceIp->v6());
    }
    else
    {
        // Pick best IP to use - extract first two bytes of dest IPv6 address
        auto destBuf = ipv6Build.destIp.v6raw();
        uint8_t firstByte = destBuf[0];
        uint8_t secondByte = destBuf[1];

        // Multicast (FF00::/8)
        if (firstByte == 0xFF)
        {
            uint8_t scope = secondByte & 0x0F; // Low nibble = bits 12-15

            if (scope == 0x01 || scope == 0x02)
            {
                auto src = ipv6Build.iface->configs.ipv6.getLocalAddress();
                if (!src.addr) return;
                ip.setSourceAddress(src.addr);
            }
            else if (scope == 0x05 || scope == 0x08) // Site/org-local
            {
                auto src = ipv6Build.iface->configs.ipv6.getLocalUnicast();
                if (!src.addr) return;
                ip.setSourceAddress(src.addr);
            }
            else if (scope == 0x0E) // core::Global scope
            {
                auto src = ipv6Build.iface->configs.ipv6.getGlobalUnicast();
                if (!src.addr) return;
                ip.setSourceAddress(src.addr);
            }
            else return; // Return if no matching scope is found
        }
        // core::Global Unicast (2000::/3 = 001xxxxx)
        else if ((firstByte & 0b11100000) == 0b00100000)
        {
            auto src = ipv6Build.iface->configs.ipv6.getGlobalUnicast();
            if (!src.addr) return;
            ip.setSourceAddress(src.addr);
        }
        // Unique Local (FC00::/7 = 1111110x)
        else if ((firstByte & 0b11111110) == 0b11111100)
        {
            auto src = ipv6Build.iface->configs.ipv6.getLocalUnicast();
            if (!src.addr) return;
            ip.setSourceAddress(src.addr);
        }
        // Link-local (FE80::/10 = 1111111010xxxxxxx)
        else if ((firstByte & 0b11111100) == 0b11111100 && (secondByte & 0b00001111) == 0x00)
        {
            auto src = ipv6Build.iface->configs.ipv6.getLocalAddress();
            if (!src.addr) return;
            ip.setSourceAddress(src.addr);
        }
        // Loopback (::1)
        else if ([&]{ for (size_t i = 0; i < 15; ++i) if (destBuf[i] != 0) return false; return destBuf[15] == 1; }())
        {
            ip.setSourceAddress(__uint128_t{1}); // ::1 loopback
        }
        else
        {
            return; // Unsuported or unconfigured destination
        }
    }
    ip.setVersionTrafficClassFlow(6, ipv6Build.DSCP, v6FlowLabel);
    ip.setNextHeader(ipv6Build.protocolType);
    ip.setHopLimit(ipv6Build.hopLimit);
    ip.setDestinationAddress(ipv6Build.destIp.v6());

    if (!ipv6Build.destMac)
    {
        uint64_t mac = 0;
        if (!getDestinationMac(mac, ipv6Build.iface, ipv6Build.destIp, ipv6Build.packetInfo))
            return; // NDP resolution in progress or failed
        ethernet::build(ipv6Build.iface, ipv6Build.packetInfo, mac, ETHERNET_IPV6);
    }
    else
    {
        ethernet::build(ipv6Build.iface, ipv6Build.packetInfo, ipv6Build.destMac.value(), ETHERNET_IPV6);
    }

    if (!ipv6Build.dontFragment)
    {
        //TODO handle fragmentation
    }
}

void reserveIpv6(processing::PacketBuilder& packetInfo)
{
    // Decide layer 2 encapsulation based on interface configs
    // DEFAULT -> Ethernet
    ethernet::reserve(packetInfo);

    size_t ipSize = packet::IPv6Header::fixedSize;
    //TODO calcualte option lengths

    packetInfo.reserveHeader(packet::HeaderType::IPV6, ipSize);
}
} // namespace infrastructure::ippacket
