#include <Decapsulation.h>
#include <immintrin.h>
#include <HeaderHelpers.hpp>

inline void prefetch_header(const void* ptr) {
    _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
}

bool inspect(PacketInfo& packet, uint8_t* data, size_t len)
{
    packet.offset = 0;

    // Layer 2
    EthernetHeader eth;
    if (unlikely(!eth.parse(data, len, packet.offset))) return false;
    packet.headers[packet.count++] = { HeaderType::ETHERNET, packet.offset };
    packet.offset += EthernetHeader::fixedSize;

    uint16_t ethernetType = readU16(eth.raw->type);

    // Layer 2.5
    while (true) 
    {
        if (ethernetType == Variable::Ethernet::vlan)
        {
            // skip
            break;
        }
        else if (ethernetType == Variable::Ethernet::mpls)
        {
            MplsHeader mpls;
            if (unlikely(!mpls.parse(data, len, packet.offset))) return false;
            packet.headers[packet.count++] = { HeaderType::MPLS, packet.offset };
            packet.offset += MplsHeader::fixedSize;

            if (likely(mpls.getBottomOfStack()))
            {
                prefetch_header(data + packet.offset);
                uint8_t ipNib = (data[packet.offset] & 0xF0) >> 4;
                ethernetType = (ipNib == 0x4) ? Variable::Ethernet::ipv4 : (ipNib == 0x6) ? Variable::Ethernet::ipv6 : 0;
                if (unlikely(!ethernetType)) return false;
            }
        }
        else if (ethernetType == Variable::Ethernet::arp)
        {
            ArpHeader arp;
            if (unlikely(!arp.parse(data, len, packet.offset))) return false;
            packet.headers[packet.count++] = { HeaderType::ARP, packet.offset };
            packet.offset += ArpHeader::fixedSize;
            return true;
        }
        else break;
    }

    // Layer 3
    if (ethernetType == Variable::Ethernet::ipv4)
    {
        size_t ipv4Size = (data[packet.offset] & 0x0F) * 4;
        IPv4Header ipv4;
        if (unlikely(!ipv4.parse(data, len, ipv4Size, packet.offset))) return false;
        packet.headers[packet.count++] = { HeaderType::IPV4, packet.offset };
        packet.offset += ipv4Size;
        prefetch_header(data + packet.offset);
        return true;
    }

    if (ethernetType == Variable::Ethernet::ipv6)
    {
        IPv6Header ipv6;
        if (unlikely(!ipv6.parse(data, len, packet.offset))) return false;
        packet.headers[packet.count++] = { HeaderType::IPV6, packet.offset };
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
            case Variable::IP::icmpv4:
            {
                IcmpHeader icmp;
                if (unlikely(!icmp.parse(data, len, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::ICMP, packet.offset };
                return true;
            }
            case Variable::IP::icmpv6:
            {
                Icmpv6Header icmp;
                if (unlikely(!icmp.parse(data, len, len - packet.offset, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::ICMPV6, packet.offset };
                return true;
            }
            case Variable::IP::tcp:
            {
                TcpHeader tcp;
                size_t tcpLen = ((data[packet.offset + 12] & 0xF0) >> 4) * 4;
                if (unlikely(!tcp.parse(data, len, tcpLen, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::TCP, packet.offset };
                packet.offset += tcpLen;
                prefetch_header(data + packet.offset);

                uint16_t src = tcp.getSourcePort();
                uint16_t dst = tcp.getDestinationPort();

                //...
                return true;
            }
            case Variable::IP::udp:
            {
                UdpHeader udp;
                if (unlikely(!udp.parse(data, len, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::UDP, packet.offset };
                packet.offset += UdpHeader::fixedSize;
                prefetch_header(data + packet.offset);

                uint16_t src = udp.getSourcePort();
                uint16_t dst = udp.getDestinationPort();

                if ((src == Variable::Udp::dhcpClient && dst == Variable::Udp::dhcpServer) ||
                    (src == Variable::Udp::dhcpServer && dst == Variable::Udp::dhcpClient))
                {
                    DhcpHeader dhcp;
                    if (unlikely(!dhcp.parse(data, len, len - packet.offset, packet.offset))) return false;
                    packet.headers[packet.count++] = { HeaderType::DHCP, packet.offset };
                    return true;
                }
                else if ((src == Variable::Udp::dhcpv6Client && dst == Variable::Udp::dhcpv6Server) ||
                         (src == Variable::Udp::dhcpv6Server && dst == Variable::Udp::dhcpv6Client))
                {
                    Dhcpv6Header dhcp6;
                    if (unlikely(!dhcp6.parse(data, len, len - packet.offset, packet.offset))) return false;
                    packet.headers[packet.count++] = { HeaderType::DHCPV6, packet.offset };
                    return true;
                }
                break;
            }
            case Variable::IP::eigrp:
            {
                EigrpHeader eigrp;
                if (unlikely(!eigrp.parse(data, len, len - packet.offset, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::EIGRP, packet.offset };
                return true;
            }
            case Variable::IP::ipv4:
            {
                IPv4Header ip;
                size_t ipLen = (data[packet.offset] & 0x0F) * 4;
                if (unlikely(!ip.parse(data, len, ipLen, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::IPV4, packet.offset };
                packet.offset += ipLen;
                break;
            }
            case Variable::IP::ipv6:
            {
                IPv6Header ip;
                if (unlikely(!ip.parse(data, len, packet.offset))) return false;
                packet.headers[packet.count++] = { HeaderType::IPV6, packet.offset };
                packet.offset += IPv6Header::fixedSize;
                break;
            }
            default:
                return false;
        }
    }

    return false;
}
