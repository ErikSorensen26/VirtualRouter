#include <Process.h>
#include <Eigrp.h>
#include <Interface.h>

// Static member definitions
ProcessPacket::ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface)
    : interface(Interface), currentVrf(vrf), currentPacket(packet), visitor(nullptr)
{ 
    print = false;

    // Initialize the visitor
    HeaderVisitor visitorStruct{ this };
    visitor = &visitorStruct;

    process(packet);
}

void ProcessPacket::process(PacketInfo& packet)
{
    // Process each layer using std::visit
    if (print) { Logger::getInstance().info() << "Layer 2:" << std::endl; }
    processLayer(layer2);
    
    if (print) { Logger::getInstance().info() << "Layer 2.5:" << std::endl; }
    processLayer(layer2_5);

    if (print) { Logger::getInstance().info() << "Layer 3:" << std::endl; }
    processLayer(layer3);

    if (print) { Logger::getInstance().info() << "Layer 4:" << std::endl; }
    processLayer(layer4);

    if (print) { Logger::getInstance().info() << "Layer 5:" << std::endl; }
    processLayer(layer5);
}

// Template function to process a layer
template <typename VariantType>
void ProcessPacket::processLayer(const std::vector<VariantType>& headers)
{
    HeaderVisitor visitorStruct{ this };
    HeaderVisitor* visitorPtr = &visitorStruct;

    for (const auto& header : headers)
    {
        std::visit(*visitorPtr, header);
    }
}

//------------------------------------------------------------------------------------
// Layer 2
//------------------------------------------------------------------------------------

void ProcessPacket::processEthernet(const EthernetHeader& eth)
{
    if (print) { Logger::getInstance().info() << "THIS IS ETHERNET" << std::endl; } 
    // Check if packet contains your source address
    macAddress = eth.sourceMac.toString();
    std::shared_lock<std::shared_mutex> lock(interface->Get()->ipMutex);
    if (eth.sourceMac.toString() == interface->Get()->macAddress.toString())
    {
        // Drop packet
        return;
    }
}

void ProcessPacket::processPpp(const PppHeader& ppp)
{
    if (print) { Logger::getInstance().info() << "THIS IS PPP" << std::endl; } 
}

void ProcessPacket::processFrame(const FrameHeader& frame)
{
    if (print) { Logger::getInstance().info() << "THIS IS FRAME" << std::endl; }
}

//------------------------------------------------------------------------------------
// Layer 2.5
//------------------------------------------------------------------------------------

void ProcessPacket::processArp(const ArpHeader& arp)
{
    if (print) { Logger::getInstance().info() << "THIS IS ARP" << std::endl; } 
    
    // Tests for request OPCODE
    if (arp.opcode == Variable::Arp::Opcode::request) {
        // Reply to arp request
        interface->arp->sendReply(arp.senderHardwareAddress, arp.senderIpAddress);
    }
    if (arp.opcode == Variable::Arp::Opcode::reply) {
        // Set as reply
        interface->arp->receiveReply(arp);
    }
}

void ProcessPacket::processMpls(const MplsHeader& mpls)
{
    if (print) { Logger::getInstance().info() << "THIS IS MPLS" << std::endl; }
}

void ProcessPacket::processVlan(const VlanHeader& vlan)
{
    if (print) { Logger::getInstance().info() << "THIS IS VLAN" << std::endl; }
}

void ProcessPacket::processLldp(const LldpHeader& lldp)
{
    if (print) { Logger::getInstance().info() << "THIS IS LLDP" << std::endl; } 
}

//------------------------------------------------------------------------------------
// Layer 3
//------------------------------------------------------------------------------------

void ProcessPacket::processIPv4(const IPv4Header& ipv4)
{
    if (print) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 
    // Validate and update the ARP table, routing talbes, ect.
    const EthernetHeader* ethernet = nullptr;
    for (const auto& eth : layer2)
    {
        if (auto ptr = std::get_if<EthernetHeader>(&eth))
        {
            ethernet = ptr;
            break;
        }
    }

    // Access EthernetHeader object
    if (ethernet && RoutingTable::getInstance().ArpLookup(ipv4.sourceAddress.toString()))
    {
        std::shared_lock<std::shared_mutex> lock(interface->Get()->ipMutex);
        RoutingTable::getInstance().updateArp(ipv4.sourceAddress.toString(), macAddress, interface->Get()->ipv4.ipAddress);
    }
}

void ProcessPacket::processIPv6(const IPv6Header& ipv6)
{
    if (print) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 
}

void ProcessPacket::processGre(const GreHeade& gre)
{
    if (print) { Logger::getInstance().info() << "THIS IS GRE" << std::endl; }
}

void ProcessPacket::processAh(const AhHeader& ah)
{
    if (print) { Logger::getInstance().info() << "THIS IS AH" << std::endl; } 
}

void ProcessPacket::processEsp(const EspHeader& esp)
{
    if (print) { Logger::getInstance().info() << "THIS IS ESP" << std::endl; } 
}

void ProcessPacket::processIcmp(const IcmpHeader& icmp)
{
    if (print) { Logger::getInstance().info() << "THIS IS ICMP" << std::endl; } 
}

void ProcessPacket::processIcmpV6(const IcmpV6Header& icmp)
{
    if (print) { Logger::getInstance().info() << "THIS IS ICMPV6" << std::endl; } 
}

void ProcessPacket::processIgmp(const IgmpHeader& igmp)
{
    if (print) { Logger::getInstance().info() << "THIS IS IGMP" << std::endl; } 
}

//------------------------------------------------------------------------------------
// Layer 4
//------------------------------------------------------------------------------------

void ProcessPacket::processTcp(const TcpHeader& tcp)
{
    if (print) { Logger::getInstance().info() << "THIS IS TCP" << std::endl; } 
}

void ProcessPacket::processUdp(const UdpHeader& udp)
{
    if (print) { Logger::getInstance().info() << "THIS IS UDP" << std::endl; } 
}

void ProcessPacket::processEigrp(const EigrpHeader& eigrp)
{
    const IPv4Header* ipv4;
    const IPv6Header* ipv6;
    if (print) { Logger::getInstance().info() << "THIS IS EIGRP" << std::endl; } 

    // Find IPv4 and IPv6 headers
    for (const auto& ip : layer3)
    {
        if (auto ptr = std::get_if<IPv4Header>(&ip))
        {
            ipv4 = ptr;
            break;
        }
    }
    for (const auto& ip : layer3)
    {
        if (auto ptr = std::get_if<IPv6Header>(&ip))
        {
            ipv6 = ptr;
            break;
        }
    }

    auto it = eigrpAutonomousSystems.find(Functions::byteToNum(eigrp.autonomousSystem));
    auto iface = interface->eigrpInterfaceList.find((Functions::byteToNum(eigrp.autonomousSystem)));
    if (it != eigrpAutonomousSystems.end() && iface != interface->eigrpInterfaceList.end()) 
    {
        uint32_t AS = Functions::byteToNum(eigrp.autonomousSystem);
        if (ipv4 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv4)
        {
            interface->eigrpInterfaceList[AS]->IPv4->processPacket(&eigrp, ipv4->sourceAddress.toString());
        }
        else if (ipv6 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv6)
        {
            interface->eigrpInterfaceList[AS]->IPv6->processPacket(&eigrp, ipv6->sourceAddress);
        }
    }
}

//------------------------------------------------------------------------------------
// Layer 4
//------------------------------------------------------------------------------------

void ProcessPacket::processDhcp(const DhcpHeader& dhcp)
{
    if (print) { Logger::getInstance().info() << "THIS IS DHCP" << std::endl; } 
    for (auto opt : dhcp.options) 
    {
        if (opt.option == Variable::Dhcp::Option::type) 
        {
            // Handle different DHCP option types.
            if (!interface->dhcp->offered && opt.value == Variable::Dhcp::Type::offer) 
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex); 
                interface->dhcp->dhcpOffer = currentPacket; 
                interface->dhcp->offered = true;
            } 
            else if ((!interface->dhcp->acked && opt.value == Variable::Dhcp::Type::ack) || (opt.value == Variable::Dhcp::Type::nak)) 
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex); 
                interface->dhcp->dhcpAck = currentPacket; 
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
                ByteString value = opt.value;
                interface->dhcp->DhcpPacket(&dhcp, value); // Handle other DHCP packets.
            }
        }
    }
}
