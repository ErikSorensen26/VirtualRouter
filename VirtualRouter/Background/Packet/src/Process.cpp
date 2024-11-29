#include <Process.h>
#include <Eigrp.h>

// Constructor for ProcessPacket class.
// Initializes the interface and processes the given packet and VRF.
ProcessPacket::ProcessPacket(PacketInfo& packet, string& vrf, Interface* Interface) : interface(Interface) 
{ 
    Process(packet, vrf); // Call the Process method to handle the packet.
}

// Processes the packet by examining and handling various header types.
void ProcessPacket::Process(PacketInfo& packet, string& vrf) 
{
    currentVrf = vrf;

    const ethernetHeader* ethernet;
    const pppHeader* ppp;
    const arpHeader* arp;
    const mplsHeader* mpls;
    const vlanHeader* vlan;
    const lldpHeader* lldp;
    const ipv4Header* ipv4;
    const ipv6Header* ipv6;
    const greHeade* gre;
    const ahHeader* ah;
    const espHeader* esp;
    const icmpHeader* icmp;
    const igmpHeader* igmp;
    const eigrpHeader* eigrp;
    const tcpHeader* tcp;
    const udpHeader* udp;
    const dhcpHeader* dhcp;

    std::string macAddress;

    // Iterate over Layer 2 headers and identify their types.
    for (auto header : packet.Layer2) 
    {
        if (is_type<ethernetHeader>(header)) 
        {
            ethernet = std::any_cast<ethernetHeader>(&header);
            if (Print(header)) { cout << "THIS IS ETHERNET" << endl; } 
            // Check if packet contains your source address
            macAddress = ethernet->sourceMac;
            if (ethernet->sourceMac == interface->Get().mac) 
            {
                // Drop packet
                return;
            }
        } 
        else if (is_type<pppHeader>(header)) 
        {
            ppp = std::any_cast<pppHeader>(&header); 
            if (Print(header)) { cout << "THIS IS PPP" << endl; } 
        }
    }

    // Iterate over Layer 2.5 headers and identify their types.
    for (auto header : packet.Layer2_5) 
    {
        if (is_type<arpHeader>(header)) 
        {
            arp = std::any_cast<arpHeader>(&header);
            if (Print(header)) { cout << "THIS IS ARP" << endl; } 
            
            // Tests for request OPCODE
            if (arp->opcode == variable.arp.opcode.request) {
                // Reply to arp request
                interface->arp->sendReply(arp->targetHardwareAddress, arp->targetIpAddress);
            }
            if (arp->opcode == variable.arp.opcode.reply) {
                // Set as reply
                interface->arp->RecieveReply(*arp);
            }
        }
        else if (is_type<mplsHeader>(header)) 
        {
            mpls = std::any_cast<mplsHeader>(&header); 
            if (Print(header)) { cout << "THIS IS MPLS" << endl; }
        } 
        else if (is_type<vlanHeader>(header)) 
        {
            vlan = std::any_cast<vlanHeader>(&header); 
            if (Print(header)) { cout << "THIS IS VLAN" << endl; }
        } 
        else if (is_type<lldpHeader>(header)) 
        {
            lldp = std::any_cast<lldpHeader>(&header);
            if (Print(header)) { cout << "THIS IS LLDP" << endl; } 
        }
    }

    // Iterate over Layer 3 headers and identify their types.
    for (auto header : packet.Layer3) 
    {
        if (is_type<ipv4Header>(header)) 
        {
            ipv4 = std::any_cast<ipv4Header>(&header);
            if (Print(header)) { cout << "THIS IS IPV4" << endl; } 
            if (ethernet && RoutingTable::getInstance().ArpLookup(ipv4->sourceAddress))
            {
                RoutingTable::getInstance().UpdateArp(ipv4->sourceAddress, macAddress, interface->Get().ip);
            }
        } 
        else if (is_type<greHeade>(header)) 
        {
            gre = std::any_cast<greHeade>(&header); 
            if (Print(header)) { cout << "THIS IS GRE" << endl; }
        } 
        else if (is_type<ahHeader>(header)) 
        {
            ah = std::any_cast<ahHeader>(&header);
            if (Print(header)) { cout << "THIS IS AH" << endl; } 
        } 
        else if (is_type<espHeader>(header)) 
        {
            esp = std::any_cast<espHeader>(&header); 
            if (Print(header)) { cout << "THIS IS ESP" << endl; } 
        } 
        else if (is_type<icmpHeader>(header)) 
        {
            icmp = std::any_cast<icmpHeader>(&header); 
            if (Print(header)) { cout << "THIS IS ICMP" << endl; } 
        } 
        else if (is_type<igmpHeader>(header)) 
        {
            igmp = std::any_cast<igmpHeader>(&header); 
            if (Print(header)) { cout << "THIS IS IGMP" << endl; } 
        }
        else if (is_type<eigrpHeader>(header)) 
        {
            if (Print(header)) { cout << "THIS IS EIGRP" << endl; } 
            eigrp = std::any_cast<eigrpHeader>(&header); 
            auto it = eigrpAutonomousSystems.find(Functions::byteToNum(eigrp->autonomousSystem));
            auto iface = interface->eigrpInterfaceList.find(Functions::byteToNum(eigrp->autonomousSystem));
            if (it != eigrpAutonomousSystems.end() && iface != interface->eigrpInterfaceList.end()) 
            {
                int AS = Functions::byteToNum(eigrp->autonomousSystem);
                if (ipv4 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv4)
                {
                    interface->eigrpInterfaceList[AS]->IPv4->ProcessPacket(eigrp, ipv4->sourceAddress);
                }
                else if (ipv6 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv6)
                {
                    interface->eigrpInterfaceList[AS]->IPv6->ProcessPacket(eigrp, ipv6->sourceAddress);
                }
            }
        }
    }

    // Iterate over Layer 4 headers and identify their types.
    for (auto header : packet.Layer4) 
    {
        if (is_type<tcpHeader>(header))
        {
            tcp = std::any_cast<tcpHeader>(&header);
            if (Print(header)) { cout << "THIS IS TCP" << endl; } 
        } 
        else if (is_type<udpHeader>(header)) 
        {
            udp = std::any_cast<udpHeader>(&header); 
            if (Print(header)) { cout << "THIS IS UDP" << endl; } 
        } 
    }

    // Iterate over Layer 5 headers and identify their types.
    for (auto header : packet.Layer5) 
    {
        if (is_type<dhcpHeader>(header)) 
        {
            dhcp = std::any_cast<dhcpHeader>(&header); 
            if (Print(header)) { cout << "THIS IS DHCP" << endl; } 
            for (auto opt : dhcp->options) 
            {
                if (opt.option == variable.dhcp.options.type) 
                {
                    // Handle different DHCP option types.
                    if (!interface->dhcp->offered && opt.value == variable.dhcp.type.offer) 
                    {
                        std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex); 
                        interface->dhcp->dhcpOffer = packet; 
                        interface->dhcp->offered = true;
                    } 
                    else if ((!interface->dhcp->acked && opt.value == variable.dhcp.type.ack) || (opt.value == variable.dhcp.type.nac)) 
                    {
                        std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex); 
                        interface->dhcp->dhcpAck = packet; 
                        interface->dhcp->acked = true;
                    } 
                    else if (opt.value == variable.dhcp.type.discover) 
                    {
                        // Handle DHCP Discover packet.
                    } 
                    else if (opt.value == variable.dhcp.type.request) 
                    {
                        // Handle DHCP Request packet.
                    } 
                    else 
                    {
                        interface->dhcp->DhcpPacket(dhcp, opt.value); // Handle other DHCP packets.
                    }
                }
            }
        }
    }
}

// Checks if the packet header should be printed based on the data type.
bool ProcessPacket::Print(any param) 
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
