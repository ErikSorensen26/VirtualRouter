#include <Process.h>
#include <EigrpInterface.h>
#include <Interface.h>
#include <VirtualRouter.h>
#include <Global.h>
#include <DhcpServer.h>
#include <DhcpClient.h>
#include <Arp.h>
#include <Ndp.h>
#include <PacketStructure.h>

#define GET_HEADER(HdrVar, HeaderType)                              \
    uint8_t* base = const_cast<uint8_t*>(data) + entry.offset;      \
    HeaderType HdrVar;                                              \
    HdrVar.setBuffer(base);

#define GET_HEADER_EXTENDED(HdrVar, HeaderType)                     \
    GET_HEADER(HdrVar, HeaderType)                                  \
    size_t trail = entry.size - HeaderType::fixedSize;              \
    if (trail != 0)                                                 \
        HdrVar.setTrail(base + HeaderType::fixedSize, trail);

void processPacket(const uint8_t* data, size_t len, PacketInfo& packet, VirtualRouter* vrf, Interface* interface)
{
    uint8_t* mac = nullptr;
    uint8_t* address = nullptr;

    const uint8_t* ipStart = nullptr;
    AddressFamily addressFamily = AddressFamily::NONE;
    
    uint32_t destinationPort = 0;
    uint32_t sourcePort = 0;

    for (uint8_t i = 0; i < packet.count; ++i)
    {
        const HeaderEntry& entry = packet.headers[i];

        switch (entry.type)
        {
            case HeaderType::ETHERNET:
            {
                GET_HEADER(eth, EthernetHeader)
                mac = eth.raw->sourceMac;
                break;
            }
            case HeaderType::ARP:
            {
                GET_HEADER(arp, ArpHeader)
                if (arp.getOpcode() == ARP_OPCODE_REQUEST) interface->arp->sendReply(arp.getSenderHwAddr(), arp.getSenderIpAddr());
                if (arp.getOpcode() == ARP_OPCODE_REPLY) interface->arp->receiveReply(arp);
                break;
            }
            case HeaderType::MPLS:
            {
                break;
            }
            case HeaderType::IPV4:
            {
                ipStart = data + entry.offset;
                GET_HEADER_EXTENDED(ipv4, IPv4Header)
                address = ipv4.getSourceAddress();
                addressFamily = AddressFamily::IPv4;
                break;
            }
            case HeaderType::IPV6:
            {
                ipStart = data + entry.offset;
                GET_HEADER(ipv6, IPv6Header);
                address = ipv6.getSourceAddress();
                addressFamily = AddressFamily::IPv6;
                break;
            }
            case HeaderType::AH:
            {
                break;
            }
            case HeaderType::ESP:
            {
                break;
            }
            case HeaderType::ICMP:
            {
                break;
            }
            case HeaderType::ICMPV6:
            {
                if (!interface || !interface->ndp) return;
                uint8_t currentIp[16];
                interface->configs.ipv6.getLocalAddress(currentIp);
                GET_HEADER_EXTENDED(icmp, Icmpv6Header)

                switch (icmp.getType())
                {
                    case 0x85:
                    {
                        if (std::memcmp(icmp.getTrail().data(), currentIp, 16) != 0) break;
                        interface->ndp->sendRouteAdvertisement(mac, icmp.getTrail().data());
                        break;
                    }
                    case 0x86:
                    {
                        interface->ndp->receiveRouteAdvertisement(icmp, address, mac);
                        break;
                    }
                    case 0x87:
                    {
                        if (std::memcmp(icmp.getTrail().data(), currentIp, 16) != 0) break;
                        uint8_t naMac[6];
                        std::vector<TLV8Option> options;
                        parseIcmpv6Options(icmp.getTrail().data(), icmp.getTrail().size(), options);
                        for (auto& opt : options)
                        {
                            if (opt.type == ICMPV6_OPTION_NDP_SOURCE && opt.valueSize == 6)
                            {
                                std::memcpy(naMac, opt.value, 6);
                                break;
                            }
                        }
                        interface->ndp->sendNeighborAdvertisement(mac, address);
                        break;
                    }
                    case 0x88:
                    {
                        interface->ndp->receiveNeighborAdvertisement(icmp, address);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
                break;
            }
            case HeaderType::TCP:
            {
                break;
            }
            case HeaderType::UDP:
            {
                GET_HEADER(udp, UdpHeader)
                sourcePort = udp.getSourcePort();
                destinationPort = udp.getDestinationPort();
                break;
            }
            case HeaderType::EIGRP:
            {
                if (!ipStart) break;
                GET_HEADER_EXTENDED(eigrp, EigrpHeader)
                uint32_t as = eigrp.getAutonomousSystem();
                auto* it = vrf->getEigrpAutonomousSystem(as);
                auto iface = interface->eigrpInterfaceList.find(as);
                if (it && iface != interface->eigrpInterfaceList.end()) 
                {
                    if (addressFamily == AddressFamily::IPv4 && interface->eigrpInterfaceList.count(as) && interface->eigrpInterfaceList[as].IPv4)
                        interface->eigrpInterfaceList[as].IPv4->getRtp().handleIncoming(ipStart, eigrp, address, Functions::isMulticast(address, AddressFamily::IPv4));
                    else if (addressFamily == AddressFamily::IPv6 && interface->eigrpInterfaceList.count(as) && interface->eigrpInterfaceList[as].IPv6)
                        interface->eigrpInterfaceList[as].IPv4->getRtp().handleIncoming(ipStart, eigrp, address, Functions::isMulticast(address, AddressFamily::IPv6));
                }
                break;
            }
            case HeaderType::DHCP:
            {
                GET_HEADER_EXTENDED(dhcp, DhcpHeader)
                if (sourcePort == UDP_DHCP_SERVER && destinationPort == UDP_DHCP_CLIENT)
                {
                    interface->dhcp->handleDhcpPacket(dhcp);
                }
                else if (sourcePort == UDP_DHCP_CLIENT && destinationPort == UDP_DHCP_SERVER)
                {
                    interface->getVRF()->global.dhcpServer->handlePacket(dhcp, mac, *interface);
                }
                break;
            }
            case HeaderType::DHCPV6:
            {
                break;
            }
            case HeaderType::DHCPV6_RELAY:
            {
                break;
            }
            case HeaderType::ENCAPSULATE:
            {
                break;
            }
            case HeaderType::NONE:
                break;
            default:
                break;
        }
    }
}
// FIXME DHCP server packets need to replace the relay field with its own ip address if its empty
