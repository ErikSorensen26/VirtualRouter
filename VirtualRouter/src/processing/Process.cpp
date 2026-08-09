// Process.cpp

#include <VirtualRouter.h>
#include <Global.h>

#include "Process.h"
#include "eigrp/core/Eigrp.h"
#include "eigrp/interface/EigrpInterface.h"
#include "interface/Interface.h"
#include "dhcp/dhcpv4/DhcpServer.h"
#include "dhcp/dhcpv4/DhcpClient.h"
#include "infrastructure/Arp.h"
#include "infrastructure/Ndp.h"
#include "packet/PacketStructure.h"

#define GET_HEADER(HdrVar, HdrType)                                         \
    uint8_t* base = const_cast<uint8_t*>(data) + entry.offset;             \
    HdrType HdrVar;                                                         \
    HdrVar.setBuffer(base);

#define GET_HEADER_EXTENDED(HdrVar, HdrType)                                \
    GET_HEADER(HdrVar, HdrType)                                             \
    size_t trail = entry.size - HdrType::fixedSize;                        \
    if (trail != 0)                                                         \
        HdrVar.setTrail(base + HdrType::fixedSize, trail);

namespace processing
{
void processPacket(const uint8_t* data, size_t len, PacketInfo& packet, core::VirtualRouter* vrf, interface::Interface* interface)
{
    uint8_t* mac = nullptr;
    uint8_t* address = nullptr;
    types::IPAddress typedAddress;

    const uint8_t* ipStart = nullptr;
    types::AddressFamily addressFamily = types::AddressFamily::NONE;
    
    uint32_t destinationPort = 0;
    uint32_t sourcePort = 0;

    for (uint8_t i = 0; i < packet.count; ++i)
    {
        const packet::HeaderEntry& entry = packet.headers[i];

        switch (entry.type)
        {
            case packet::HeaderType::ETHERNET:
            {
                GET_HEADER(eth, packet::EthernetHeader)
                mac = eth.raw->sourceMac;
                break;
            }
            case packet::HeaderType::ARP:
            {
                GET_HEADER(arp, packet::ArpHeader)
                if (arp.getOpcode() == ARP_OPCODE_REQUEST) interface->arp.sendReply(utils::read<uint64_t, 6>(arp.getSenderHwAddr()), arp.getSenderIpAddr());
                if (arp.getOpcode() == ARP_OPCODE_REPLY) interface->arp.receiveReply(arp);
                break;
            }
            case packet::HeaderType::MPLS:
            {
                break;
            }
            case packet::HeaderType::IPV4:
            {
                ipStart = data + entry.offset;
                GET_HEADER_EXTENDED(ipv4, packet::IPv4Header)
                address = ipv4.getSourceAddress();
                typedAddress = types::IPAddress(utils::read<uint32_t>(address));
                addressFamily = types::AddressFamily::IPv4;
                break;
            }
            case packet::HeaderType::IPV6:
            {
                ipStart = data + entry.offset;
                GET_HEADER(ipv6, packet::IPv6Header);
                address = ipv6.getSourceAddress();
                typedAddress = types::IPAddress(utils::read<__uint128_t>(address));
                addressFamily = types::AddressFamily::IPv6;
                break;
            }
            case packet::HeaderType::AH:
            {
                break;
            }
            case packet::HeaderType::ESP:
            {
                break;
            }
            case packet::HeaderType::ICMP:
            {
                break;
            }
            case packet::HeaderType::ICMPV6:
            {
                if (!interface || interface->ndp.isShutdown()) return;
                auto currentAddr = interface->configs.ipv6.getLocalAddress();
                GET_HEADER_EXTENDED(icmp, packet::Icmpv6Header)

                switch (icmp.getType())
                {
                    case 0x85:
                    {
                        if (types::IPv6Address{utils::read<__uint128_t>(icmp.getTrail().data())}.addr != currentAddr.addr) break;
                        interface->ndp.sendRouteAdvertisement(utils::read<uint64_t, 6>(mac), types::IPv6Address{utils::read<__uint128_t>(icmp.getTrail().data())});
                        break;
                    }
                    case 0x86:
                    {
                        interface->ndp.receiveRouteAdvertisement(icmp, types::IPv6Address{utils::read<__uint128_t>(address)}, utils::read<uint64_t, 6>(mac));
                        break;
                    }
                    case 0x87:
                    {
                        if (types::IPv6Address{utils::read<__uint128_t>(icmp.getTrail().data())}.addr != currentAddr.addr) break;
                        uint8_t naMac[6];
                        std::vector<packet::TLV8Option> options;
                        parseIcmpv6Options(icmp.getTrail().data(), icmp.getTrail().size(), options);
                        for (auto& opt : options)
                        {
                            if (opt.type == ICMPV6_OPTION_NDP_SOURCE && opt.valueSize == 6)
                            {
                                std::memcpy(naMac, opt.value, 6);
                                break;
                            }
                        }
                        interface->ndp.sendNeighborAdvertisement(utils::read<uint64_t, 6>(mac), types::IPv6Address{utils::read<__uint128_t>(address)});
                        break;
                    }
                    case 0x88:
                    {
                        interface->ndp.receiveNeighborAdvertisement(icmp, types::IPv6Address{utils::read<__uint128_t>(address)});
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
                break;
            }
            case packet::HeaderType::TCP:
            {
                break;
            }
            case packet::HeaderType::UDP:
            {
                GET_HEADER(udp, packet::UdpHeader)
                sourcePort = udp.getSourcePort();
                destinationPort = udp.getDestinationPort();
                break;
            }
            case packet::HeaderType::EIGRP:
            {
                if (!ipStart) break;
                GET_HEADER_EXTENDED(eigrp, packet::EigrpHeader)
                uint32_t as = eigrp.getAutonomousSystem();
                auto* it = vrf->getEigrpAutonomousSystem(as);
                if (it)
                {
                    auto ifaceKey = interface->configs.key;
                    if (addressFamily == types::AddressFamily::IPv4 && it->ipv4)
                    {
                        auto* eigrpIface = it->ipv4->getIfaceMgr().getInterface(ifaceKey);
                        if (eigrpIface) eigrpIface->getRtp().handleIncoming(ipStart, eigrp, typedAddress, typedAddress.isMulticast());
                    }
                    else if (addressFamily == types::AddressFamily::IPv6 && it->ipv6)
                    {
                        auto* eigrpIface = it->ipv6->getIfaceMgr().getInterface(ifaceKey);
                        if (eigrpIface) eigrpIface->getRtp().handleIncoming(ipStart, eigrp, typedAddress, typedAddress.isMulticast());
                    }
                }
                break;
            }
            case packet::HeaderType::DHCP:
            {
                GET_HEADER_EXTENDED(dhcp, packet::DhcpHeader)
                if (sourcePort == UDP_DHCP_SERVER && destinationPort == UDP_DHCP_CLIENT)
                {
                    interface->dhcp->handleDhcpPacket(dhcp);
                }
                else if (sourcePort == UDP_DHCP_CLIENT && destinationPort == UDP_DHCP_SERVER)
                {
                    interface->getVRF()->getGlobal().dhcpServer->handlePacket(dhcp, mac, *interface);
                }
                break;
            }
            case packet::HeaderType::DHCPV6:
            {
                break;
            }
            case packet::HeaderType::DHCPV6_RELAY:
            {
                break;
            }
            case packet::HeaderType::ENCAPSULATE:
            {
                break;
            }
            case packet::HeaderType::NONE:
                break;
            default:
                break;
        }
    }
}
// FIXME DHCP server packets need to replace the relay field with its own ip address if its empty

} // namespace processing
