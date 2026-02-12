// Decapsulation.cpp

#include <immintrin.h>

#include "Decapsulation.h"
#include "packet/PacketStructure.h"
#include "packet/HeaderHelpers.hpp"

inline void prefetch_header(const void* ptr) {
    _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
}

bool inspect(PacketInfo& packet, uint8_t* data, size_t len)
{
    packet.offset = 0;

    // Layer 2
    EthernetHeader eth;
    if (unlikely(!eth.parse(data, len, packet.offset))) return false;
    packet.headers[packet.count++] = { HeaderType::ETHERNET, packet.offset, EthernetHeader::fixedSize };
    packet.offset += EthernetHeader::fixedSize;

    uint16_t ethernetType = readU16(eth.raw->type);

    // Layer 2.5
    while (true) 
    {
        if (ethernetType == ETHERNET_VLAN)
        {
            // skip
            break;
        }
        else if (ethernetType == ETHERNET_MPLS)
        {
            MplsHeader mpls;
            if (unlikely(!mpls.parse(data, len, packet.offset))) return false;
            packet.headers[packet.count++] = { HeaderType::MPLS, packet.offset, MplsHeader::fixedSize };
            packet.offset += MplsHeader::fixedSize;

            if (likely(mpls.getBottomOfStack()))
            {
                prefetch_header(data + packet.offset);
                uint8_t ipNib = (data[packet.offset] & 0xF0) >> 4;
                ethernetType = (ipNib == 0x4) ? ETHERNET_IPV4 : (ipNib == 0x6) ? ETHERNET_IPV6 : 0;
                if (unlikely(!ethernetType)) return false;
            }
        }
        else if (ethernetType == ETHERNET_ARP)
        {
            ArpHeader arp;
            if (unlikely(!arp.parse(data, len, packet.offset))) return false;
            packet.headers[packet.count++] = { HeaderType::ARP, packet.offset, ArpHeader::fixedSize };
            packet.offset += ArpHeader::fixedSize;
            return true;
        }
        else break;
    }

    // Layer 3
    if (ethernetType == ETHERNET_IPV4)
    {
        size_t ipv4Size = (data[packet.offset] & 0x0F) * 4;
        IPv4Header ipv4;
        if (unlikely(!ipv4.parse(data, len, ipv4Size, packet.offset))) return false;
        packet.headers[packet.count++] = { HeaderType::IPV4, packet.offset, ipv4Size };
        packet.offset += ipv4Size;
        prefetch_header(data + packet.offset);
        return true;
    }

    if (ethernetType == ETHERNET_IPV6)
    {
        IPv6Header ipv6;
        if (unlikely(!ipv6.parse(data, len, packet.offset))) return false;
        packet.headers[packet.count++] = { HeaderType::IPV6, packet.offset, IPv6Header::fixedSize };
        packet.offset += IPv6Header::fixedSize;
        prefetch_header(data + packet.offset);
        return true;
    }

    return false;
}

bool decapsulate(PacketInfo& packet, uint8_t* data, size_t len)
{
    if (packet.offset == len) return true;

    while (likely(packet.offset < len))
    {
        HeaderType lastType = packet.headers[packet.count - 1].type;
        uint8_t proto = 0;

        // Layer 3 again for robust decapsulation
        if (lastType == HeaderType::IPV4)
        {
            auto* ip = reinterpret_cast<const IPv4HeaderRaw*>(data + packet.headers[packet.count - 1].offset);
            proto = ip->protocol;
        }
        else if (lastType == HeaderType::IPV6)
        {
            auto* ip = reinterpret_cast<const IPv6HeaderRaw*>(data + packet.headers[packet.count - 1].offset);
            proto = ip->nextHeader;
        }
        else return false;

        // Layer 4-7
        switch (proto)
        {
            case IP_ICMPV4:
            {
                IcmpHeader icmp;
                if (unlikely(!icmp.parse(data, len, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::ICMP, packet.offset, IcmpHeader::fixedSize };
                return true;
            }
            case IP_ICMPV6:
            {
                Icmpv6Header icmp;
                size_t headerSize = len - packet.offset;
                if (unlikely(!icmp.parse(data, len, headerSize, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::ICMPV6, packet.offset, headerSize };
                return true;
            }
            case IP_TCP:
            {
                if (len < packet.offset + 13)
                    return false;
                TcpHeader tcp;
                size_t tcpLen = ((data[packet.offset + 12] & 0xF0) >> 4) * 4;
                if (tcpLen < 20 || len < packet.offset + tcpLen)
                    return false;
                if (unlikely(!tcp.parse(data, len, tcpLen, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::TCP, packet.offset, tcpLen };
                packet.offset += tcpLen;
                prefetch_header(data + packet.offset);

                uint16_t src = tcp.getSourcePort();
                uint16_t dst = tcp.getDestinationPort();

                //...
                return true;
            }
            case IP_UDP:
            {
                UdpHeader udp;
                if (unlikely(!udp.parse(data, len, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::UDP, packet.offset, UdpHeader::fixedSize };
                packet.offset += UdpHeader::fixedSize;
                prefetch_header(data + packet.offset);

                uint16_t src = udp.getSourcePort();
                uint16_t dst = udp.getDestinationPort();

                if ((src == UDP_DHCP_CLIENT && dst == UDP_DHCP_SERVER) ||
                    (src == UDP_DHCP_SERVER && dst == UDP_DHCP_CLIENT))
                {
                    DhcpHeader dhcp;
                    size_t dhcpSize = len - packet.offset;
                    if (unlikely(!dhcp.parse(data, len, dhcpSize, packet.offset))) return false;
                    packet.headers[packet.count++] = { HeaderType::DHCP, packet.offset, dhcpSize };
                    return true;
                }
                else if ((src == UDP_DHCPV6_CLIENT && dst == UDP_DHCPV6_SERVER) ||
                         (src == UDP_DHCPV6_SERVER && dst == UDP_DHCPV6_CLIENT))
                {
                    Dhcpv6Header dhcp6;
                    size_t dhcpSize = len - packet.offset;
                    if (unlikely(!dhcp6.parse(data, len, dhcpSize, packet.offset))) return false;
                    packet.headers[packet.count++] = { HeaderType::DHCPV6, packet.offset, dhcpSize };
                    return true;
                }
                break;
            }
            case IP_EIGRP:
            {
                EigrpHeader eigrp;
                size_t eigrpSize = len - packet.offset;
                if (unlikely(!eigrp.parse(data, len, eigrpSize, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::EIGRP, packet.offset, eigrpSize };
                return true;
            }
            case IP_IPV4:
            {
                IPv4Header ip;
                size_t ipLen = (data[packet.offset] & 0x0F) * 4;
                if (unlikely(!ip.parse(data, len, ipLen, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::IPV4, packet.offset, ipLen };
                packet.offset += ipLen;
                break;
            }
            case IP_IPV6:
            {
                IPv6Header ip;
                if (unlikely(!ip.parse(data, len, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::IPV6, packet.offset, IPv6Header::fixedSize };
                packet.offset += IPv6Header::fixedSize;
                break;
            }
            default:
                return false;
        }
    }

    return false;
}
