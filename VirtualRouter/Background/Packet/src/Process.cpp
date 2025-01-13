#include <Process.h>
#include <Eigrp.h>
#include <Interface.h>

// Static member definitions
std::unordered_map<std::type_index, ProcessPacket::HeaderProcessor> ProcessPacket::layer2Processors;
std::unordered_map<std::type_index, ProcessPacket::HeaderProcessor> ProcessPacket::layer2_5Processors;
std::unordered_map<std::type_index, ProcessPacket::HeaderProcessor> ProcessPacket::layer3Processors;
std::unordered_map<std::type_index, ProcessPacket::HeaderProcessor> ProcessPacket::layer4Processors;
std::unordered_map<std::type_index, ProcessPacket::HeaderProcessor> ProcessPacket::layer5Processors;

ProcessPacket::ProcessPacket(PacketInfo& packet, ByteString& vrf, Interface* Interface) : interface(Interface), currentVrf(vrf), currentPacket(packet)
{ 
    static bool initialized = false;
    if (!initialized)
    {
        initializeProcessors();
        initialized = true;
    }
    process(packet); // Call the Process method to handle the packet.
}

void ProcessPacket::initializeProcessors() 
{
    // Layer 2
    layer2Processors[typeid(EthernetHeader)] = [this](const std::any& header) { processEthernet(header); };
    layer2Processors[typeid(PppHeader)] = [this](const std::any& header) { processPpp(header); };

    // Layer 2.5
    layer2_5Processors[typeid(ArpHeader)] = [this](const std::any& header) { processArp(header); };
    layer2_5Processors[typeid(MplsHeader)] = [this](const std::any& header) { processMpls(header); };
    layer2_5Processors[typeid(VlanHeader)] = [this](const std::any& header) { processVlan(header); };
    layer2_5Processors[typeid(LldpHeader)] = [this](const std::any& header) { processLldp(header); };

    // Layer 3
    layer3Processors[typeid(IPv4Header)] = [this](const std::any& header) { processIPv4(header); };
    layer3Processors[typeid(IPv6Header)] = [this](const std::any& header) { processIPv6(header); };
    layer3Processors[typeid(GreHeade)] = [this](const std::any& header) { processGre(header); };
    layer3Processors[typeid(AhHeader)] = [this](const std::any& header) { processAh(header); };
    layer3Processors[typeid(EspHeader)] = [this](const std::any& header) { processEsp(header); };
    layer3Processors[typeid(IcmpHeader)] = [this](const std::any& header) { processIcmp(header); };
    layer3Processors[typeid(IgmpHeader)] = [this](const std::any& header) { processIgmp(header); };
    layer3Processors[typeid(EigrpHeader)] = [this](const std::any& header) { processEigrp(header); };

    // Layer 4
    layer4Processors[typeid(TcpHeader)] = [this](const std::any& header) { processTcp(header); };
    layer4Processors[typeid(UdpHeader)] = [this](const std::any& header) { processUdp(header); };

    // Layer 5
    layer5Processors[typeid(DhcpHeader)] = [this](const std::any& header) { processDhcp(header); };
}

void ProcessPacket::process(PacketInfo& packet)
{
    processLayer(packet.Layer2, layer2Processors);
    processLayer(packet.Layer2_5, layer2_5Processors);
    processLayer(packet.Layer3, layer3Processors);
    processLayer(packet.Layer4, layer4Processors);
    processLayer(packet.Layer5, layer4Processors);
}

void ProcessPacket::processLayer(
    const std::vector<std::any>& headers,
    const std::unordered_map<std::type_index, HeaderProcessor>& layerProcessors)
{
    for (const auto& header : headers)
    {
        parsedHeaders[header.type()] = header; // Store header in parsedHeaders
        auto it = layerProcessors.find(header.type());
        if (it != layerProcessors.end())
        {
            it->second(header);
        }
    }
}

//------------------------------------------------------------------------------------
// Layer 2
//------------------------------------------------------------------------------------

void ProcessPacket::processEthernet(const std::any& header)
{
    const EthernetHeader* ethernet = std::any_cast<EthernetHeader>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS ETHERNET" << std::endl; } 
    // Check if packet contains your source address
    macAddress = ethernet->sourceMac.toString();
    std::shared_lock<std::shared_mutex> lock(interface->Get()->ipMutex);
    if (ethernet->sourceMac == interface->Get()->macAddress)
    {
        // Drop packet
        return;
    }
}

void ProcessPacket::processPpp(const std::any& header)
{
    const PppHeader* ppp = std::any_cast<PppHeader>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS PPP" << std::endl; } 
}

//------------------------------------------------------------------------------------
// Layer 2.5
//------------------------------------------------------------------------------------

void ProcessPacket::processArp(const std::any& header)
{
    const ArpHeader* arp = std::any_cast<ArpHeader>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS ARP" << std::endl; } 
    
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

void ProcessPacket::processMpls(const std::any& header)
{
    const MplsHeader* mpls = std::any_cast<MplsHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS MPLS" << std::endl; }
}

void ProcessPacket::processVlan(const std::any& header)
{
    const VlanHeader* vlan = std::any_cast<VlanHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS VLAN" << std::endl; }
}

void ProcessPacket::processLldp(const std::any& header)
{
    const LldpHeader* lldp = std::any_cast<LldpHeader>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS LLDP" << std::endl; } 
}

//------------------------------------------------------------------------------------
// Layer 3
//------------------------------------------------------------------------------------

void ProcessPacket::processIPv4(const std::any& header)
{
    const IPv4Header* ipv4 = std::any_cast<IPv4Header>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 
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

    // Access EthernetHeader object
    const EthernetHeader* ethernet = std::any_cast<EthernetHeader>(&parsedHeaders[typeid(EthernetHeader)]);
    if (ethernet && RoutingTable::getInstance().ArpLookup(ipv4->sourceAddress))
    {
        std::shared_lock<std::shared_mutex> lock(interface->Get()->ipMutex);
        RoutingTable::getInstance().updateArp(ipv4->sourceAddress, macAddress, interface->Get()->ipv4.ipAddress);
    }
}

void ProcessPacket::processIPv6(const std::any& header)
{
    const IPv6Header* ipv6 = std::any_cast<IPv6Header>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 
}

void ProcessPacket::processGre(const std::any& header)
{
    const GreHeade* gre = std::any_cast<GreHeade>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS GRE" << std::endl; }
}

void ProcessPacket::processAh(const std::any& header)
{
    const AhHeader* ah = std::any_cast<AhHeader>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS AH" << std::endl; } 
}

void ProcessPacket::processEsp(const std::any& header)
{
    const EspHeader* esp = std::any_cast<EspHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS ESP" << std::endl; } 
}

void ProcessPacket::processIcmp(const std::any& header)
{
    const IcmpHeader* icmp = std::any_cast<IcmpHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS ICMP" << std::endl; } 
}

void ProcessPacket::processIgmp(const std::any& header)
{
    const IgmpHeader* igmp = std::any_cast<IgmpHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS IGMP" << std::endl; } 
}

void ProcessPacket::processEigrp(const std::any& header)
{
    const EigrpHeader* eigrp = std::any_cast<EigrpHeader>(&header); 
    const IPv4Header* ipv4 = std::any_cast<IPv4Header>(&parsedHeaders[typeid(IPv4Header)]);
    const IPv6Header* ipv6 = std::any_cast<IPv6Header>(&parsedHeaders[typeid(IPv6Header)]);
    if (print) { Logger::getInstance().info() << "THIS IS EIGRP" << std::endl; } 
    auto it = eigrpAutonomousSystems.find(Functions::byteToNum(eigrp->autonomousSystem));
    auto iface = interface->eigrpInterfaceList.find((Functions::byteToNum(eigrp->autonomousSystem)));
    if (it != eigrpAutonomousSystems.end() && iface != interface->eigrpInterfaceList.end()) 
    {
        uint32_t AS = Functions::byteToNum(eigrp->autonomousSystem);
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

//------------------------------------------------------------------------------------
// Layer 4
//------------------------------------------------------------------------------------

void ProcessPacket::processTcp(const std::any& header)
{
    const TcpHeader* tcp = std::any_cast<TcpHeader>(&header);
    if (print) { Logger::getInstance().info() << "THIS IS TCP" << std::endl; } 
}

void ProcessPacket::processUdp(const std::any& header)
{
    const UdpHeader* udp = std::any_cast<UdpHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS UDP" << std::endl; } 
}

//------------------------------------------------------------------------------------
// Layer 4
//------------------------------------------------------------------------------------

void ProcessPacket::processDhcp(const std::any& header)
{
    const DhcpHeader* dhcp = std::any_cast<DhcpHeader>(&header); 
    if (print) { Logger::getInstance().info() << "THIS IS DHCP" << std::endl; } 
    for (auto opt : dhcp->options) 
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
                interface->dhcp->DhcpPacket(dhcp, value); // Handle other DHCP packets.
            }
        }
    }
}
