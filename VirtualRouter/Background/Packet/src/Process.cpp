#include <Process.h>
#include <Eigrp.h>

// Constructor for ProcessPacket class.
// Initializes the interface and processes the given packet and VRF.
ProcessPacket::ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface) : interface(Interface) 
{ 
    process(packet, vrf); // Call the Process method to handle the packet.
}

// Processes the packet by examining and handling various header types.
void ProcessPacket::process(PacketInfo& packet, ByteString& vrf) 
{
    currentVrf = vrf;

    EthernetHeader* ethernet;
    PppHeader* ppp;
    ArpHeader* arp;
    MplsHeader* mpls;
    VlanHeader* vlan;
    LldpHeader* lldp;
    IPv4Header* ipv4;
    IPv6Header* ipv6;
    GreHeade* gre;
    AhHeader* ah;
    EspHeader* esp;
    IcmpHeader* icmp;
    IgmpHeader* igmp;
    EigrpHeader* eigrp;
    TcpHeader* tcp;
    UdpHeader* udp;
    DhcpHeader* dhcp;

    ByteString macAddress;

    // Iterate over Layer 2 headers and identify their types.
    for (auto header : packet.Layer2) 
    {
        if (is_type<EthernetHeader>(header)) 
        {
            ethernet = std::any_cast<EthernetHeader>(&header);
            if (print(header)) { Logger::getInstance().info() << "THIS IS ETHERNET" << std::endl; } 
            // Check if packet contains your source address
            macAddress = ethernet->sourceMac.toString();
            if (ethernet->sourceMac == interface->Get().macAddress) 
            {
                // Drop packet
                return;
            }
        } 
        else if (is_type<PppHeader>(header)) 
        {
            ppp = std::any_cast<PppHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS PPP" << std::endl; } 
        }
    }

    // Iterate over Layer 2.5 headers and identify their types.
    for (auto header : packet.Layer2_5) 
    {
        if (is_type<ArpHeader>(header)) 
        {
            arp = std::any_cast<ArpHeader>(&header);
            if (print(header)) { Logger::getInstance().info() << "THIS IS ARP" << std::endl; } 
            
            // Tests for request OPCODE
            if (arp->opcode == Variable::Arp::Opcode::request) {
                // Reply to arp request
                interface->arp->sendReply(arp->senderHardwareAddress, arp->senderIpAddress);
            }
            if (arp->opcode == Variable::Arp::Opcode::reply) {
                // Set as reply
                interface->arp->receiveReply(*arp);
            }
        }
        else if (is_type<MplsHeader>(header)) 
        {
            mpls = std::any_cast<MplsHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS MPLS" << std::endl; }
        } 
        else if (is_type<VlanHeader>(header)) 
        {
            vlan = std::any_cast<VlanHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS VLAN" << std::endl; }
        } 
        else if (is_type<LldpHeader>(header)) 
        {
            lldp = std::any_cast<LldpHeader>(&header);
            if (print(header)) { Logger::getInstance().info() << "THIS IS LLDP" << std::endl; } 
        }
    }

    // Iterate over Layer 3 headers and identify their types.
    for (auto header : packet.Layer3) 
    {
        if (is_type<IPv4Header>(header)) 
        {
            ipv4 = std::any_cast<IPv4Header>(&header);
            if (print(header)) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 
            // Validate adjacent IP address
            // if (ipv4->sourceAddress != interface->adjacentIp)
            // {
            //     interface->adjacentIp = ipv4->sourceAddress;
            //     if (Functions::compareNetworkWithIp(interface->Get().ip, ipv4->sourceAddress, interface->Get().subnet))
            //     {
            //         interface->adjacentValid = true;
            //     }
            //     else
            //     {
            //         interface->adjacentValid = false;
            //     }
            // }
            // if (!interface->adjacentValid)
            // {
            //     return;
            // }
            if (ethernet && RoutingTable::getInstance().ArpLookup(ipv4->sourceAddress))
            {
                RoutingTable::getInstance().updateArp(ipv4->sourceAddress, macAddress, interface->Get().ipAddress);
            }
        } 
        else if (is_type<GreHeade>(header)) 
        {
            gre = std::any_cast<GreHeade>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS GRE" << std::endl; }
        } 
        else if (is_type<AhHeader>(header)) 
        {
            ah = std::any_cast<AhHeader>(&header);
            if (print(header)) { Logger::getInstance().info() << "THIS IS AH" << std::endl; } 
        } 
        else if (is_type<EspHeader>(header)) 
        {
            esp = std::any_cast<EspHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS ESP" << std::endl; } 
        } 
        else if (is_type<IcmpHeader>(header)) 
        {
            icmp = std::any_cast<IcmpHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS ICMP" << std::endl; } 
        } 
        else if (is_type<IgmpHeader>(header)) 
        {
            igmp = std::any_cast<IgmpHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS IGMP" << std::endl; } 
        }
        else if (is_type<EigrpHeader>(header)) 
        {
            if (print(header)) { Logger::getInstance().info() << "THIS IS EIGRP" << std::endl; } 
            eigrp = std::any_cast<EigrpHeader>(&header); 
            auto it = eigrpAutonomousSystems.find(Functions::byteToNum(eigrp->autonomousSystem.toString()));
            auto iface = interface->eigrpInterfaceList.find(Functions::byteToNum(eigrp->autonomousSystem.toString()));
            if (it != eigrpAutonomousSystems.end() && iface != interface->eigrpInterfaceList.end()) 
            {
                int AS = Functions::byteToNum(eigrp->autonomousSystem.toString());
                if (ipv4 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv4)
                {
                    interface->eigrpInterfaceList[AS]->IPv4->processPacket(eigrp, ipv4->sourceAddress);
                }
                else if (ipv6 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv6)
                {
                    interface->eigrpInterfaceList[AS]->IPv6->processPacket(eigrp, ipv6->sourceAddress);
                }
            }
        }
    }

    // Iterate over Layer 4 headers and identify their types.
    for (auto header : packet.Layer4) 
    {
        if (is_type<TcpHeader>(header))
        {
            tcp = std::any_cast<TcpHeader>(&header);
            if (print(header)) { Logger::getInstance().info() << "THIS IS TCP" << std::endl; } 
        } 
        else if (is_type<UdpHeader>(header)) 
        {
            udp = std::any_cast<UdpHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS UDP" << std::endl; } 
        } 
    }

    // Iterate over Layer 5 headers and identify their types.
    for (auto header : packet.Layer5) 
    {
        if (is_type<DhcpHeader>(header)) 
        {
            dhcp = std::any_cast<DhcpHeader>(&header); 
            if (print(header)) { Logger::getInstance().info() << "THIS IS DHCP" << std::endl; } 
            for (auto opt : dhcp->options) 
            {
                if (opt.option == Variable::Dhcp::Option::type) 
                {
                    // Handle different DHCP option types.
                    if (!interface->dhcp->offered && opt.value == Variable::Dhcp::Type::offer) 
                    {
                        std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex); 
                        interface->dhcp->dhcpOffer = packet; 
                        interface->dhcp->offered = true;
                    } 
                    else if ((!interface->dhcp->acked && opt.value == Variable::Dhcp::Type::ack) || (opt.value == Variable::Dhcp::Type::nac)) 
                    {
                        std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex); 
                        interface->dhcp->dhcpAck = packet; 
                        interface->dhcp->acked = true;
                    } 
                    else if (opt.value == Variable::Dhcp::Type::discover) 
                    {
                        // Handle DHCP Discover packet.
                    } 
                    else if (opt.value == Variable::Dhcp::Type::request) 
                    {
                        // Handle DHCP Request packet.
                    } 
                    else 
                    {
                        std::string value = opt.value.toString();
                        interface->dhcp->DhcpPacket(dhcp, value); // Handle other DHCP packets.
                    }
                }
            }
        }
    }
}

// Checks if the packet header should be printed based on the data type.
bool ProcessPacket::print(std::any param) 
{
    if (data.empty()) 
    {
        return true;
    } 
    else 
    {
        for (auto str : data) 
        {
            if (param.type() == str.type()) 
            {
                return true;
            }
        }
    }
    return false;
}
