#include <Process.h>
#include <Eigrp.h>
#include <Interface.h>
#include <VirtualRouter.h>
#include <Dhcp.h>
#include <Arp.h>
#include <Ndp.h>

// Static member definitions
ProcessPacket::ProcessPacket(PacketInfo& packet, VirtualRouter* vrf, Interface* Interface)
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
    processLayer(currentPacket.Layer2);
    
    if (print) { Logger::getInstance().info() << "Layer 2.5:" << std::endl; }
    processLayer(currentPacket.Layer2_5);

    if (print) { Logger::getInstance().info() << "Layer 3:" << std::endl; }
    processLayer(currentPacket.Layer3);

    if (print) { Logger::getInstance().info() << "Layer 4:" << std::endl; }
    processLayer(currentPacket.Layer4);

    if (print) { Logger::getInstance().info() << "Layer 5:" << std::endl; }
    processLayer(currentPacket.Layer5);
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

//------------------------------------------------------------------------------------
// Layer 3
//------------------------------------------------------------------------------------

void ProcessPacket::processIPv4(const IPv4Header& ipv4)
{
    if (print) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 
    
    isMulticast = Functions::isMulticast(ipv4.destinationAddress);
    ipAddress = ipv4.sourceAddress;
    addressFamily = AddressFamily::IPv4;
}

void ProcessPacket::processIPv6(const IPv6Header& ipv6)
{
    if (print) { Logger::getInstance().info() << "THIS IS IPV4" << std::endl; } 

    isMulticast = Functions::isMulticast(ipv6.destinationAddress);
    ipAddress = ipv6.sourceAddress;
    addressFamily = AddressFamily::IPv6;
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
    ByteString currentIp;
    if (!interface || !interface->ndp)
    {
        return;
    }
    else
    {
        currentIp = interface->configs.ipv6.getLocalAddress();
        if (currentIp.size() != 16) return; // Invalid IP
    }

    switch (icmp.type[0].value)
    {
        case 0x85:
        {
            if (icmp.payload != currentIp) break;
            //TODO check nextHeader on ipv6
            interface->ndp->sendRouteAdvertisement(macAddress, icmp.payload);
            break;
        }
        case 0x86:
        {
            interface->ndp->receiveRouteAdvertisement(icmp, ipAddress, macAddress);
            break;
        }
        case 0x87:
        {
            if (icmp.payload != currentIp) break;
            ByteString mac;
            for (auto& opt : icmp.options)
            {
                if (opt.option == Variable::ICMPv6::Option::source && opt.value.size() == 6)
                {
                    mac = opt.value;
                    break;
                }
            }
            interface->ndp->sendNeighborAdvertisement(mac, &ipAddress);
            break;
        }
        case 0x88:
        {
            interface->ndp->receiveNeighborAdvertisement(icmp, ipAddress);
            break;
        }
        default:
        {
            break;
        }
    }
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
    for (const auto& ip : currentPacket.Layer3)
    {
        if (auto ptr = std::get_if<IPv4Header>(&ip))
        {
            ipv4 = ptr;
            break;
        }
    }
    for (const auto& ip : currentPacket.Layer3)
    {
        if (auto ptr = std::get_if<IPv6Header>(&ip))
        {
            ipv6 = ptr;
            break;
        }
    }

    auto* it = currentVrf->getEigrpAutonomousSystem(Functions::byteToNum(eigrp.autonomousSystem));
    auto iface = interface->eigrpInterfaceList.find((Functions::byteToNum(eigrp.autonomousSystem)));
    if (it && iface != interface->eigrpInterfaceList.end()) 
    {
        uint32_t AS = Functions::byteToNum(eigrp.autonomousSystem);
        if (addressFamily == AddressFamily::IPv4 && ipv4 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv4)
        {
            interface->eigrpInterfaceList[AS]->IPv4->processPacket(eigrp, ipv4->sourceAddress, isMulticast);
        }
        else if (addressFamily == AddressFamily::IPv6 && ipv6 && interface->eigrpInterfaceList[AS] && interface->eigrpInterfaceList[AS]->IPv6)
        {
            interface->eigrpInterfaceList[AS]->IPv6->processPacket(eigrp, ipv6->sourceAddress, isMulticast);
        }
    }
}

//------------------------------------------------------------------------------------
// Layer 5
//------------------------------------------------------------------------------------

void ProcessPacket::processDhcp(const DhcpHeader& dhcp)
{
    ByteString mac = interface->configs.getMac();

    if (print) { Logger::getInstance().info() << "THIS IS DHCP" << std::endl; } 
    for (auto opt : dhcp.options) 
    {
        if (opt.option == Variable::Dhcp::Option::type) 
        {
            // Handle differen DHCP option types using a switch-case for efficiency
            if (opt.value == Variable::Dhcp::Type::discover)
            {
                //TODO Handle DHCP Discover packet
            }
            else if (opt.value == Variable::Dhcp::Type::offer)
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex);
                interface->dhcp->dhcpOffer = currentPacket;
                interface->dhcp->processDhcpResponses(interface->routingInstance->global.getHostname(), mac);
            }
            else if (opt.value == Variable::Dhcp::Type::request)
            {
                //TODO Handle DHCP Request packet
            }
            else if (opt.value == Variable::Dhcp::Type::ack)
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex);
                interface->dhcp->dhcpAck = currentPacket;
                interface->dhcp->processDhcpResponses(interface->routingInstance->global.getHostname(), mac);
            }
            else if (opt.value == Variable::Dhcp::Type::nak)
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex);
                interface->dhcp->dhcpNak = currentPacket;
                interface->dhcp->processDhcpResponses(interface->routingInstance->global.getHostname(), mac);
            }
            else if (opt.value == Variable::Dhcp::Type::decline)
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex);
                interface->dhcp->dhcpDecline = currentPacket;
                interface->dhcp->processDhcpResponses(interface->routingInstance->global.getHostname(), mac);
            }
            else if (opt.value == Variable::Dhcp::Type::release)
            {
                //TODO Handle DHCP release packet
            }
            else if (opt.value == Variable::Dhcp::Type::inform)
            {
                std::lock_guard<std::mutex> lock(interface->dhcp->dhcpMutex);
                interface->dhcp->processDhcpResponses(interface->routingInstance->global.getHostname(), mac);
            }
            else
            {
                ByteString value = opt.value;
                interface->dhcp->DhcpPacket(&dhcp, value);
            }
        }
    }
}

void ProcessPacket::processDhcpv6(const Dhcpv6Header& header)
{
    
}

void ProcessPacket::processDhcpv6Relay(const Dhcpv6RelayHeader& header)
{

}


// FIXME DHCP server packets need to replace the relay field with its own ip address if its empty
