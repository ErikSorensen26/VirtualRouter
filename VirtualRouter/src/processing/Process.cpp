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

#define getHeader(HeaderType) reinterpret_cast<const HeaderType*>(base); \

void processPacket(const uint8_t* data, size_t len, PacketInfo& packet, VirtualRouter* vrf, Interface* interface)
{
    for (uint8_t i = 0; i < packet.count; ++i)
    {
        const HeaderEntry& entry = packet.headers[i];
        const uint8_t* base = data + entry.offset;

        uint8_t* mac = nullptr;
        uint8_t* address = nullptr;
        AddressFamily addressFamily = AddressFamily::NONE;

        const uint8_t* ipStart = nullptr;
        uint32_t destinationPort = 0;
        uint32_t sourcePort = 0;
        
        switch (entry.type)
        {
            case HeaderType::ETHERNET:
            {
                const auto* eth = getHeader(EthernetHeader);
                mac = eth->raw->sourceMac;
                break;
            }
            case HeaderType::ARP:
            {
                const auto* arp = getHeader(ArpHeader);
                if (arp->getOpcode() == Variable::Arp::Opcode::request) interface->arp->sendReply(arp->getSenderHwAddr(), arp->getSenderIpAddr());
                if (arp->getOpcode() == Variable::Arp::Opcode::reply) interface->arp->receiveReply(*arp);
                break;
            }
            case HeaderType::MPLS:
            {
                break;
            }
            case HeaderType::IPV4:
            {
                ipStart = data + entry.offset;
                const auto* ipv4 = getHeader(IPv4Header);
                address = ipv4->getSourceAddress();
                addressFamily = AddressFamily::IPv4;
                break;
            }
            case HeaderType::IPV6:
            {
                ipStart = data + entry.offset;
                const auto* ipv6 = getHeader(IPv6Header);
                address = ipv6->getSourceAddress();
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
                const auto* icmp = getHeader(Icmpv6Header);

                switch (icmp->getType())
                {
                    case 0x85:
                    {
                        if (std::memcmp(icmp->getTrail().data(), currentIp, 16) != 0) break;
                        interface->ndp->sendRouteAdvertisement(mac, icmp->getTrail().data());
                        break;
                    }
                    case 0x86:
                    {
                        interface->ndp->receiveRouteAdvertisement(*icmp, address, mac);
                        break;
                    }
                    case 0x87:
                    {
                        if (std::memcmp(icmp->getTrail().data(), currentIp, 16) != 0) break;
                        uint8_t naMac[6];
                        std::vector<TLV8Option> options;
                        parseIcmpv6Options(icmp->getTrail().data(), icmp->getTrail().size(), options);
                        for (auto& opt : options)
                        {
                            if (opt.type == Variable::ICMPv6::Option::source && opt.valueSize == 6)
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
                        interface->ndp->receiveNeighborAdvertisement(*icmp, address);
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
                const auto* udp = getHeader(UdpHeader);
                sourcePort = udp->getSourcePort();
                destinationPort = udp->getDestinationPort();
                break;
            }
            case HeaderType::EIGRP:
            {
                if (!ipStart) break;
                const auto* eigrp = getHeader(EigrpHeader);
                uint32_t as = eigrp->getAutonomousSystem();
                auto* it = vrf->getEigrpAutonomousSystem(as);
                auto iface = interface->eigrpInterfaceList.find(as);
                if (it && iface != interface->eigrpInterfaceList.end()) 
                {
                    if (addressFamily == AddressFamily::IPv4 && interface->eigrpInterfaceList.count(as) && interface->eigrpInterfaceList[as].IPv4)
                        interface->eigrpInterfaceList[as].IPv4->getRtp().handleIncoming(ipStart, *eigrp, address, Functions::isMulticast(address, AddressFamily::IPv4));
                    else if (addressFamily == AddressFamily::IPv6 && interface->eigrpInterfaceList.count(as) && interface->eigrpInterfaceList[as].IPv6)
                        interface->eigrpInterfaceList[as].IPv4->getRtp().handleIncoming(ipStart, *eigrp, address, Functions::isMulticast(address, AddressFamily::IPv6));
                }
                break;
            }
            case HeaderType::DHCP:
            {
                const auto* dhcp = getHeader(DhcpHeader);
                if (sourcePort == Variable::Udp::dhcpServer && destinationPort == Variable::Udp::dhcpServer)
                {
                    interface->dhcp->handleDhcpPacket(*dhcp);
                }
                else if (sourcePort == Variable::Udp::dhcpClient && destinationPort == Variable::Udp::dhcpClient)
                {
                    interface->routingInstance->global.dhcpServer->handlePacket(*dhcp, mac, *interface);
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
