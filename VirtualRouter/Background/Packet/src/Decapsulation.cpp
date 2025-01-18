#include <Decapsulation.h>
#include <Profiler.hpp>

// Constructor for Packet class, starts packet inspection
Packet::Packet(ByteString &packet, bool debug, Interface& iface)
     : currentInterface(&iface),
      start(0),
      fullPacket(packet),
      print(debug)
{
    if (print) {Logger::getInstance().info() << packet.toHex() << std::endl;}
    inspection(packet);
}

Packet::Packet(ByteString& packet) : fullPacket(packet), currentInterface(nullptr) {}

// Inspects the given packet and processes each layer.
bool Packet::inspection(ByteString &packet)
{
    //Profiler::getInstance().notify("Inspection begin");
    start = 0;

    if (!processLayer2(packet) ||
        !processLayer2_5(packet) ||
        !processLayer3(packet)) return false;

    return true;
}

// Decapsulates the remaining layers after Layer 3.
bool Packet::decapsulate()
{
    if (!processLayer4(fullPacket) ||
        !processLayer5(fullPacket)) return false;
    return true;
}

// Slices the packet for decapsulation
ByteString Packet::getSlice(size_t length)
{
    if (!validateSize(start, length, fullPacket))
    {
        return {};
    }
    ByteString slice = fullPacket.substr(start, length);
    start += length;
    return slice;
}

// Handles Layer 2 processing, distinguishing between Ethernet and PPP.
bool Packet::processLayer2(ByteString &packet)
{
    //Profiler::getInstance().notify("Decapsulating Layer 2 Headers");
    //if (print) { Logger::getInstance().info() << "Layer 2:" << std::endl; }
    const GreHeade* gre = getLayer3Header<GreHeade>();
    if (gre && gre->protocol == Variable::Gre::ppp && validateSize(start, 4, packet))
    {
        ByteString pppHeader = getSlice(4);
        if (!decodePpp(pppHeader)) return false;
    }
    else if (validateSize(start, 14, packet))
    {
        ByteString ethernetHeader = getSlice(14);
        if (!decodeEthernet(ethernetHeader)) return false;
    }
    else return false;

    return true;
}

// Processes Layer 2.5 headers such as ARP, MPLS, VLAN, or LLDP.
bool Packet::processLayer2_5(ByteString &packet)
{
    //std::cout << "l2_5 " << packet.toHex() << std::endl;
    //Profiler::getInstance().notify("Decapsulating Layer 2.5 Headers");
    //if (print) { Logger::getInstance().info() << "Layer 2.5:" << std::endl; }
    const EthernetHeader* ethernet = getLayer2Header<EthernetHeader>();
    if (ethernet && ethernet->type == Variable::Ethernet::arp)
    {
        if (!validateSize(start, 28, packet)) return false;
        ByteString arpHeader = getSlice(28);
        if (!decodeArp(arpHeader)) return false;
    }
    else if (ethernet && ethernet->type == Variable::Ethernet::mpls)
    {
        if (!validateSize(start, 4, packet)) return false;
        ByteString mplsHeader = getSlice(4).toHex();
        if (!decodeMpls(mplsHeader)) return false;
    }
    else if (ethernet && ethernet->type == Variable::Ethernet::vlan)
    {
        if (!validateSize(start, 4, packet)) return false;
        ByteString vlanHeader = getSlice(4);
        if (!decodeVlan(vlanHeader)) return false;
    }
    else if (ethernet && ethernet->type == Variable::Ethernet::lldp)
    {
        ByteString lldpHeader = packet.substr(start);
        if (!decodeLldp(lldpHeader)) return false;
    }
    afterPacket = packet.substr(start);
    return true;
}

// Processes Layer 3 headers, focusing on IPv4/IPv6 and its encapsulated protocols.
bool Packet::processLayer3(ByteString &packet)
{
    //Profiler::getInstance().notify("Decapsulating Layer 3 Headers");
    //if (print) { Logger::getInstance().info() << "Layer 3:" << std::endl; }
    const EthernetHeader* ethernet = getLayer2Header<EthernetHeader>();
    const VlanHeader* vlan = getLayer2_5Header<VlanHeader>();

    // IPv4
    if (ethernet && (ethernet->type == Variable::Ethernet::ipv4 || ethernet->type == Variable::Ethernet::mpls || (vlan && vlan->type == Variable::Ethernet::ipv4)))
    {
        size_t ipv4Size = Functions::binToNum((Functions::byteToBin(packet.substr(start, 1))).substr(4, 4)) * 4;
        if (!validateSize(start, ipv4Size, packet)) return false;
        ByteString ipv4Header = getSlice(ipv4Size);
        if (!decodeIPv4(ipv4Header, ipv4Size)) return false;
        const IPv4Header* ipv4 = getLayer3Header<IPv4Header>();

        if (ipv4 && ipv4->protocol.toString() == Variable::IP::gre)
        {
            if (!validateSize(start, 12, packet)) return false;
            ByteString greHeader = getSlice(12);
            if (!decodeGre(greHeader)) return false;
            const GreHeade* gre = getLayer3Header<GreHeade>();
            if (gre && gre->protocol == Variable::Gre::ppp)
            {
                ByteString grepacket = packet.substr(start);
                if (!inspection(grepacket)) return false;
                return true;
            }
        }
        else if (ipv4 && ipv4->protocol.toString() == Variable::IP::ah)
        {
            size_t ahSize = (Functions::binToNum(Functions::byteToBin(packet.substr(start + 1, 1))) * 4) + 8;
            if (!validateSize(start, ahSize, packet)) return false;
            ByteString ahHeader = getSlice(ahSize);
            if (!decodeAh(ahHeader, ahSize)) return false;
            const AhHeader* ah = getLayer3Header<AhHeader>();
            if (ah && ah->next == Variable::Ah::esp)
            {
                ByteString espHeader = packet.substr(start);
                if (!decodeEsp(espHeader)) return false;
            }
        }
        else if (ipv4 && ipv4->protocol.toString() == Variable::Ah::esp)
        {
            ByteString espHeader = packet.substr(start);
            if (!decodeEsp(espHeader)) return false;
        }
        else if (ipv4 && ipv4->protocol.toString() == Variable::IP::icmpv4)
        {
            if (!validateSize(start, 8, packet)) return false;
            ByteString icmpHeader = getSlice(8);
            if (!decodeIcmp(icmpHeader)) return false;
        }
        else if (ipv4 && ipv4->protocol.toString() == Variable::IP::igmp)
        {
            ByteString igmpHeader = packet.substr(start);
            if (!decodeIgmp(igmpHeader)) return false;
        }
    }
    if (ethernet->type.toString() == Variable::Ethernet::ipv6)
    {
        if (!validateSize(start, 40, packet)) return false;
        ByteString ipv6Header = getSlice(40);
        if (!decodeIPv6(ipv6Header)) return false;
        const IPv6Header* ipv6 = getLayer3Header<IPv6Header>();

        if (ipv6 && ipv6->protocol == Variable::IP::icmpv6)
        {
            if (!validateSize(start, 8, packet)) return false;
            ByteString icmpHeader = packet.substr(start);
            if (!decodeIcmpV6(icmpHeader)) return false;
        }
    }
    afterPacket = packet.substr(start);
    return true;
}

// Processes Layer 4 headers, including TCP, UDP, and EIGRP.
bool Packet::processLayer4(ByteString &packet)
{
    //if (print) { Logger::getInstance().info() << "Layer 4:" << std::endl; }
    //Profiler::getInstance().notify("Decapsulating Layer 4 Headers");
    const IPv4Header* ipv4 = getLayer3Header<IPv4Header>();
    const IPv6Header* ipv6 = getLayer3Header<IPv6Header>();
    if (ipv4 && ipv4->protocol.toString() == Variable::IP::tcp)
    {
        if (!validateSize(start, 20, packet)) return false;
        size_t tcpSize = Functions::binToNum((Functions::byteToBin(packet.substr(start + 12, 1))).substr(0, 4)) * 4;
        if (!validateSize(start, tcpSize, packet)) return false;
        ByteString tcpHeader = getSlice(tcpSize);
        if (!decodeTcp(tcpHeader, tcpSize)) return false;
    }
    else if (ipv4 && ipv4->protocol.toString() == Variable::IP::udp)
    {
        if (!validateSize(start, 8, packet)) return false;
        ByteString udpHeader = getSlice(8);
        if (!decodeUdp(udpHeader)) return false;
    }
    else if (ipv4 && ipv4->protocol.toString() == Variable::IP::eigrp)
    {
        size_t eigrpSize = static_cast<size_t>(Functions::byteToNum(ipv4->totalLength.toString()) - 20);
        if (!validateSize(start, eigrpSize, packet)) return false;
        ByteString eigrpHeader = getSlice(eigrpSize);
        if (!decodeEigrp(eigrpHeader)) return false;
    }
    afterPacket = packet.substr(start);
    return true;
}

// Processes Layer 5 (Session Layer) headers, such as DHCP.
bool Packet::processLayer5(ByteString &packet)
{
    //if (print) { Logger::getInstance().info() << "Layer 5:" << std::endl; }
    //Profiler::getInstance().notify("Decapsulating Layer 5 Headers");
    const UdpHeader* udp = getLayer4Header<UdpHeader>();
    if (udp && ((udp->sourcePort == Variable::Udp::Dhcp::source && udp->destinationPort == Variable::Udp::Dhcp::destination) ||
        (udp->sourcePort == Variable::Udp::Dhcp::destination && udp->destinationPort == Variable::Udp::Dhcp::source)))
    {
        if (!validateSize(start, 240, packet)) return false;
        ByteString dhcpHeader = packet.substr(start);
        if (!decodeDhcp(dhcpHeader)) return false;
    }
    return true;
}

//-----------------------------------------------------------------------------------------------
// Layer 2
//-----------------------------------------------------------------------------------------------

// Parses and processes Ethernet header.
bool Packet::decodeEthernet(ByteString &ethernetHeader)
{
    //std::cout << "eth " << ethernetHeader.toHex() << std::endl;
    //Profiler::getInstance().notify("decoding ethernet");
    EthernetHeader ethernet;
    if (!ethernet.decapsulate(ethernetHeader)) return false;

    ByteString currentMac;
    {
        if (currentInterface)
        {
            std::shared_lock<std::shared_mutex> lock(currentInterface->Get()->ipMutex);
            currentMac = currentInterface->Get()->macAddress;
        }
    }

    if (currentMac == ethernet.sourceMac.toString())
    {
        Logger::getInstance().info() << "Packet dropped due to receiving current MAC" << std::endl;
        return false;
    }

    if (print)
    {
        Logger::getInstance().info() << "Ethernet:" << std::endl;
        Logger::getInstance().info() << "Destination Mac: " << ethernet.destinationMac.toHex() << std::endl;
        Logger::getInstance().info() << "Source Mac: " << ethernet.sourceMac.toHex() << std::endl;
        Logger::getInstance().info() << "Type: " << ethernet.type.toHex() << std::endl;
    }

    packetInfo.Layer2.emplace_back(ethernet);
    //Profiler::getInstance().notify("decode complete");
    return true;
}

// Parses and processes the PPP header.
bool Packet::decodePpp(ByteString &pppHeader)
{
    //std::cout << "ppp " << pppHeader.toHex() << std::endl;
    PppHeader ppp;
    if (!ppp.decapsulate(pppHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "PPP: " << std::endl;
        Logger::getInstance().info() << "Address: " << ppp.address.toHex() << std::endl;
        Logger::getInstance().info() << "Control: " << ppp.control.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol: " << ppp.protocol.toHex() << std::endl;
    }

    packetInfo.Layer2.push_back(std::move(ppp));
    return true;
}

// Parses and processes the Frame Relay header.
bool Packet::decodeFrame(ByteString &frameHeader)
{
    //std::cout << "frame " << frameHeader.toHex() << std::endl;
    FrameHeader frame;
    if (!frame.decapsulate(frameHeader)) return true;

    if (print)
    {

        Logger::getInstance().info() << "Frame Relay:" << std::endl;
        Logger::getInstance().info() << "First DLCI: " << frame.firstAddress.dlci << std::endl;
        Logger::getInstance().info() << "First CR: " << frame.firstAddress.cr << std::endl;
        Logger::getInstance().info() << "First EA: " << frame.firstAddress.ea << std::endl;
        Logger::getInstance().info() << "Second DLCI: " << frame.secondAddress.dlci << std::endl;
        Logger::getInstance().info() << "Second FECN: " << frame.secondAddress.fecn << std::endl;
        Logger::getInstance().info() << "Second BECN: " << frame.secondAddress.becn << std::endl;
        Logger::getInstance().info() << "Second DE: " << frame.secondAddress.de << std::endl;
        Logger::getInstance().info() << "Second EA: " << frame.secondAddress.ea << std::endl;
        Logger::getInstance().info() << "Type: " << frame.type.toHex() << std::endl;
    }

    packetInfo.Layer2.push_back(std::move(frame));
    return true;
}

//-----------------------------------------------------------------------------------------------
// Layer 2.5
//-----------------------------------------------------------------------------------------------

// Parses and processes ARP header.
bool Packet::decodeArp(ByteString &arpHeader)
{
    //std::cout << "arp " << arpHeader.toHex() << std::endl;
    ArpHeader arp;
    if (!arp.decapsulate(arpHeader)) return false;

    if (print)
    {
        Logger::getInstance().info() << "ARP Header:" << std::endl;
        Logger::getInstance().info() << "Hardware Type: " << arp.hardwareType.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol Type: " << arp.protocolType.toHex() << std::endl;
        Logger::getInstance().info() << "Hardware Size: " << arp.hardwareSize.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol Size: " << arp.protocolSize.toHex() << std::endl;
        Logger::getInstance().info() << "Opcode: " << arp.opcode.toHex() << std::endl;
        Logger::getInstance().info() << "Sender Hardware Address: " << arp.senderHardwareAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Sender IP Address: " << arp.senderIpAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Target Hardware Address: " << arp.targetHardwareAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Target IP Address: " << arp.targetIpAddress.toHex() << std::endl;
    }

    packetInfo.Layer2_5.push_back(std::move(arp));
    return true;
}

// Parses and processes the MPLS header.
bool Packet::decodeMpls(ByteString &mplsHeader)
{
    //std::cout << "mpls " << mplsHeader.toHex() << std::endl;
    MplsHeader mpls;
    if (!mpls.decapsulate(mplsHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "MPLS Header:" << std::endl;
        Logger::getInstance().info() << "Label: " << mpls.label << std::endl;
        Logger::getInstance().info() << "Exp Bit: " << mpls.expBit << std::endl;
        Logger::getInstance().info() << "Bottom Label Stack: " << mpls.bottomLabelStack << std::endl;
        Logger::getInstance().info() << "TTL: " << mpls.TTL << std::endl;
    }

    packetInfo.Layer2_5.push_back(std::move(mpls));
    return true;
}

// Parses and processes the VLAN header.
bool Packet::decodeVlan(ByteString &vlanHeader)
{
    //std::cout << "vlan " << vlanHeader.toHex() << std::endl;
    VlanHeader vlan;
    if (!vlan.decapsulate(vlanHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "Vlan: " << std::endl;
        Logger::getInstance().info() << "Priority: " << vlan.priority << std::endl;
        Logger::getInstance().info() << "DEI: " << vlan.dei << std::endl;
        Logger::getInstance().info() << "ID: " << vlan.id << std::endl;
        Logger::getInstance().info() << "Type: " << vlan.type.toHex() << std::endl;
    }

    packetInfo.Layer2_5.push_back(std::move(vlan));
    return true;
}

// Parses and processes the LLDP header.
bool Packet::decodeLldp(ByteString &lldpHeader)
{
    //std::cout << "lldp " << lldpHeader.toHex() << std::endl;
    LldpHeader tempLlsp;

//     size_t pos = 0;
//     while (pos < lldpHeader.size())
//     {
// 
//         LldpHeader::TLV tlv;
// 
//         ByteString sec = Functions::byteToBin(lldpHeader.substr(pos, 2));
//         tlv.type = Functions::binToByte(sec.substr(0, 7), 1);
//         tlv.length = Functions::binToByte(sec.substr(7, 9), 2);
//         pos += 2;
// 
//         tlv.value = lldpHeader.substr(pos, tlv.length);
//         pos += tlv;
// 
//         switch (tlv.type)
//         {
//         case 1:
//             lldp.chassisID = tlv;
//             break;
//         case 2:
//             lldp.portID = tlv;
//             break;
//         case 3:
//             lldp.ttl = tlv;
//             break;
//         case 4:
//             lldp.portDescription = tlv;
//             break;
//         case 5:
//             lldp.systemName = tlv;
//             break;
//         case 6:
//             lldp.systemDescription = tlv;
//             break;
//         case 7:
//             lldp.systemCapabilities = tlv;
//             break;
//         case 8:
//             lldp.managementAddress = tlv;
//             break;
//         case 127:
//             lldp.organizationallySpecific = tlv;
//             break;
//         case 0:
//             lldp.endOfLLDPDU = tlv;
//             break;
//         default:
//             break;
//         }
//         if (tlv.type == 0)
//         {
//             break;
//         }
//     }

//     if (print)
//     {
// 
//         Logger::getInstance().info() << "LLDP Frame:" << std::endl;
//         Logger::getInstance().info() << "Chassis ID: " << lldp->chassisID.value << std::endl;
//         Logger::getInstance().info() << "Port ID: " << lldp->portID.value << std::endl;
//         Logger::getInstance().info() << "TTL: " << lldp->ttl.value << std::endl;
//         Logger::getInstance().info() << "Port Description: " << lldp->portDescription.value << std::endl;
//         Logger::getInstance().info() << "System Name: " << lldp->systemName.value << std::endl;
//         Logger::getInstance().info() << "System Description: " << lldp->systemDescription.value << std::endl;
//         Logger::getInstance().info() << "System Capabilities: " << lldp->systemCapabilities.value << std::endl;
//         Logger::getInstance().info() << "Management Address: " << lldp->managementAddress.value << std::endl;
//         Logger::getInstance().info() << "Organizationally Specific: " << lldp->organizationallySpecific.value << std::endl;
//         Logger::getInstance().info() << "End of LLDPDU" << std::endl;
//     }
    return false;
}

//-----------------------------------------------------------------------------------------------
// Layer 3
//-----------------------------------------------------------------------------------------------

// Parses and processes the IP header.
bool Packet::decodeIPv4(ByteString &ipv4Header, size_t &ipv4Size)
{
    //Profiler::getInstance().notify("ip decode");
    //std::cout << "ipv4 " << ipv4Header.toHex() << std::endl;
    IPv4Header ipv4;
    if (!ipv4.decapsulate(ipv4Header)) return false;

    if (print)
    {

        Logger::getInstance().info() << "IPv4 Header:" << std::endl;
        Logger::getInstance().info() << "Version: " << ipv4.version << std::endl;
        Logger::getInstance().info() << "Header Length: " << ipv4.headerLength << std::endl;
        Logger::getInstance().info() << "Service Field: " << ipv4.serviceField.toHex() << std::endl;
        Logger::getInstance().info() << "Total Length: " << Functions::hexToNum(ipv4.totalLength.toHex()) << " " << this->fullPacket.size() << std::endl;
        Logger::getInstance().info() << "Identification: " << ipv4.identification.toHex() << std::endl;
        Logger::getInstance().info() << "TTL: " << ipv4.TTL.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol: " << ipv4.protocol.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << ipv4.checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Source Address: " << Functions::byteAddressToNumAddress(ipv4.sourceAddress.toString()) << std::endl;
        Logger::getInstance().info() << "Destination Address: " << Functions::byteAddressToNumAddress(ipv4.destinationAddress.toString()) << std::endl;
        Logger::getInstance().info() << "Fragment Flags:" << std::endl;
        Logger::getInstance().info() << "  Reserved: " << ipv4.fragmentFlag.reserved << std::endl;
        Logger::getInstance().info() << "  Fragment: " << ipv4.fragmentFlag.fragment << std::endl;
        Logger::getInstance().info() << "  More Fragment: " << ipv4.fragmentFlag.moreFragment << std::endl;
        Logger::getInstance().info() << "  Fragment Offset: " << ipv4.fragmentFlag.fragmentOffset << std::endl;
    }

    if (ipv4Size > 20 && ipv4Header.size() == 23)
    {
        if (print)
        {

            Logger::getInstance().info() << "Options:" << std::endl;
            Logger::getInstance().info() << "  Type:" << std::endl;
            Logger::getInstance().info() << "    Copy: " << ipv4.options.type.copy << std::endl;
            Logger::getInstance().info() << "    Class Control: " << ipv4.options.type.classControl << std::endl;
            Logger::getInstance().info() << "    Router Alert: " << ipv4.options.type.routerAlert << std::endl;
            Logger::getInstance().info() << "  Length: " << ipv4.options.length << std::endl;
            Logger::getInstance().info() << "  Router Alert: " << ipv4.options.routerAlert << std::endl;
        }
    }

    packetInfo.Layer3.emplace_back(ipv4);
    //Profiler::getInstance().notify("ip decode end");
    return true;
}

// Parses and processes the IPv6 header
bool Packet::decodeIPv6(ByteString &ipv6Header)
{
    //std::cout << "ipv6 " << ipv6Header.toHex() << std::endl;
    IPv6Header ipv6;
    if (!ipv6.decapsulate(ipv6Header)) return false;

    if (print)
    {
        Logger::getInstance().info() << "IPv6 Header:" << std::endl;
        Logger::getInstance().info() << "Version: " << ipv6.version.toHex() << std::endl;
        Logger::getInstance().info() << "Flow Label: " << Functions::byteToBin(ipv6.flowLabel) << std::endl;
        Logger::getInstance().info() << "Payload Length: " << Functions::byteToNum(ipv6.payloadLength) << std::endl;
        Logger::getInstance().info() << "Protocol: " << ipv6.protocol.toHex() << std::endl;
        Logger::getInstance().info() << "Hop Limit: " << Functions::byteToNum(ipv6.hopLimit) << std::endl;
        Logger::getInstance().info() << "Source Address: " << ipv6.sourceAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Destination Address: " << ipv6.destinationAddress.toHex() << std::endl;
    }

    packetInfo.Layer3.push_back(std::move(ipv6));
    return true;
}

// Parses and processes the ICMP header.
bool Packet::decodeIcmp(ByteString &icmpHeader)
{
    //std::cout << "icmp " << icmpHeader.toHex() << std::endl;
    IcmpHeader icmp;
    if (!icmp.decapsulate(icmpHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "ICMP Header:" << std::endl;
        Logger::getInstance().info() << "Type: " << icmp.type.toHex() << std::endl;
        Logger::getInstance().info() << "Code: " << icmp.code.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << icmp.checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Identifier: " << icmp.identifier.toHex() << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << icmp.sequenceNumber.toHex() << std::endl;
    }

    packetInfo.Layer3.push_back(std::move(icmp));
    return true;
}

// Parses and processes the ICMPv6 header
bool Packet::decodeIcmpV6(ByteString &icmpV6Header)
{
    //std::cout << "icmpv6 " << icmpV6Header.toHex() << std::endl;
    IcmpV6Header icmp;
    if (!icmp.decapsulate(icmpV6Header)) return false;

    packetInfo.Layer3.push_back(std::move(icmp));
    return true;
}

// Parses and processes the IGMP header.
bool Packet::decodeIgmp(ByteString &igmpHeader)
{
    //std::cout << "igmp " << igmpHeader.toHex() << std::endl;
    IgmpHeader igmp;
    if (!igmp.decapsulate(igmpHeader)) return false;

    if (print) {

        Logger::getInstance().info() << "IGMP Header:" << std::endl;
        Logger::getInstance().info() << "Type: " << igmp.type.toHex() << std::endl;
        Logger::getInstance().info() << "Max Rest Time: " << igmp.maxRestTime.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << igmp.checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Multicast Address: " << igmp.multicastAddress.toHex() << std::endl;
    }

    if (igmpHeader.size() == 12 && print) {
        Logger::getInstance().info() << "IGMP v3:" << std::endl;
        Logger::getInstance().info() << "  Suppress: " << igmp.v3.supress << std::endl;
        Logger::getInstance().info() << "  QRV: " << igmp.v3.qrv << std::endl;
        Logger::getInstance().info() << "  QQIC: " << igmp.v3.qqic << std::endl;
        Logger::getInstance().info() << "  Num Src: " << Functions::byteToHex(igmp.v3.numSrc) << std::endl;
    }

    packetInfo.Layer3.push_back(std::move(igmp));
    return true;
}

// Parses and processes the GRE header.
bool Packet::decodeGre(ByteString &greHeader)
{
    //std::cout << "gre " << greHeader.toHex() << std::endl;
    GreHeade gre;
    if (!gre.decapsulate(greHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "Gre: " << std::endl;
        Logger::getInstance().info() << "Flags: " << std::endl;
        Logger::getInstance().info() << "   Checksum: " << gre.flags.checksum << std::endl;
        Logger::getInstance().info() << "   Routing: " << gre.flags.routing << std::endl;
        Logger::getInstance().info() << "   Key: " << gre.flags.key << std::endl;
        Logger::getInstance().info() << "   Sequence Number: " << gre.flags.seqNum << std::endl;
        Logger::getInstance().info() << "   Strict Source Route: " << gre.flags.strictSourceRoute << std::endl;
        Logger::getInstance().info() << "   Recursion: " << gre.flags.recursion << std::endl;
        Logger::getInstance().info() << "   Acknowledgment: " << gre.flags.acknowledgment << std::endl;
        Logger::getInstance().info() << "   Reserved: " << gre.flags.reserved << std::endl;
        Logger::getInstance().info() << "   Version: " << gre.flags.version << std::endl;
        Logger::getInstance().info() << "Protocol: " << gre.protocol.toHex() << std::endl;
        Logger::getInstance().info() << "Length: " << gre.length.toHex() << std::endl;
        Logger::getInstance().info() << "Call ID: " << gre.callID.toHex() << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << gre.seqNum.toHex() << std::endl;
    }

    packetInfo.Layer3.push_back(std::move(gre));
    return true;
}

// Parses and processes the AH header.
bool Packet::decodeAh(ByteString &ahHeader, size_t &ahSize)
{
    //std::cout << "ah " << ahHeader.toHex() << std::endl;
    AhHeader ah;
    if (!ah.decapsulate(ahHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "AH:" << std::endl;
        Logger::getInstance().info() << "Next: " << ah.next.toHex() << std::endl;
        Logger::getInstance().info() << "Length: " << ah.length.toHex() << std::endl;
        Logger::getInstance().info() << "Reserved: " << ah.reserved.toHex() << std::endl;
        Logger::getInstance().info() << "AH SPI: " << ah.spi.toHex() << std::endl;
        Logger::getInstance().info() << "AH Sequence: " << ah.sequence.toHex() << std::endl;
        Logger::getInstance().info() << "AH ICV: " << ah.icv.toHex() << std::endl;
    }

    packetInfo.Layer3.push_back(std::move(ah));
    return true;
}

// Parses and processes the ESP header.
bool Packet::decodeEsp(ByteString &espHeader)
{
    //std::cout << "esp " << espHeader.toHex() << std::endl;
    EspHeader esp;
    if (!esp.decapsulate(espHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "ESP: " << std::endl;
        Logger::getInstance().info() << "ESP SPI: " << esp.spi.toHex() << std::endl;
        Logger::getInstance().info() << "ESP Sequence: " << esp.sequence.toHex() << std::endl;
    }

    packetInfo.Layer3.push_back(std::move(esp));
    return true;
}

//-----------------------------------------------------------------------------------------------
// Layer 4
//-----------------------------------------------------------------------------------------------

// Parses and processes the TCP header.
bool Packet::decodeTcp(ByteString &tcpHeader, size_t &tcpSize)
{
    //std::cout << "tcp " << tcpHeader.toHex() << std::endl;
    TcpHeader tcp;
    if (!tcp.decapsulate(tcpHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "TCP Header:" << std::endl;
        Logger::getInstance().info() << "Source Port: " << tcp.sourcePort.toHex() << std::endl;
        Logger::getInstance().info() << "Destination Port: " << tcp.destinationPort.toHex() << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << tcp.sequenceNumber.toHex() << std::endl;
        Logger::getInstance().info() << "Ack Number: " << tcp.ackNumber.toHex() << std::endl;
        Logger::getInstance().info() << "Header Length: " << tcp.headerLength.toHex() << std::endl;
        Logger::getInstance().info() << "Window Size: " << tcp.windowSize.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << tcp.checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Urgent Pointer: " << tcp.urgentPointer.toHex() << std::endl;
        Logger::getInstance().info() << "Flags:" << std::endl;
        Logger::getInstance().info() << "  Congestion Window Reduced: " << tcp.flags.congestionWindowReduced << std::endl;
        Logger::getInstance().info() << "  ECN Echo: " << tcp.flags.ecnEcho << std::endl;
        Logger::getInstance().info() << "  Urgent: " << tcp.flags.urgent << std::endl;
        Logger::getInstance().info() << "  Acknowledgement: " << tcp.flags.acknowledgement << std::endl;
        Logger::getInstance().info() << "  Push: " << tcp.flags.push << std::endl;
        Logger::getInstance().info() << "  Reset: " << tcp.flags.reset << std::endl;
        Logger::getInstance().info() << "  SYN: " << tcp.flags.syn << std::endl;
        Logger::getInstance().info() << "  FIN: " << tcp.flags.fin << std::endl;
    }

    packetInfo.Layer4.push_back(std::move(tcp));
    return true;
}

// Parses and processes the UDP header.
bool Packet::decodeUdp(ByteString &udpHeader)
{
    //std::cout << "udp " << udpHeader.toHex() << std::endl;
    UdpHeader udp;
    if (!udp.decapsulate(udpHeader)) return false;

    if (print)
    {

        Logger::getInstance().info() << "UDP Header:" << std::endl;
        Logger::getInstance().info() << "Source Port: " << udp.sourcePort.toHex() << std::endl;
        Logger::getInstance().info() << "Destination Port: " << udp.destinationPort.toHex() << std::endl;
        Logger::getInstance().info() << "Length: " << udp.length.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << udp.checksum.toHex() << std::endl;
    }

    packetInfo.Layer4.push_back(std::move(udp));
    return true;
}

// Parses and processes the EIGRP header.
bool Packet::decodeEigrp(ByteString &eigrpHeader)
{
    //std::cout << "eigrp " << eigrpHeader.toHex() << std::endl;
    EigrpHeader eigrp;
    if (!eigrp.decapsulate(eigrpHeader)) return false;

    if (print)
    {
        Logger::getInstance().info() << "EIGRP Frame:" << std::endl;
        Logger::getInstance().info() << "Version: " << eigrp.version.toHex() << std::endl;
        Logger::getInstance().info() << "Opcode: " << eigrp.opcode.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << eigrp.checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Flags: " << std::endl;
        Logger::getInstance().info() << "  Init: " << eigrp.flags.init << std::endl;
        Logger::getInstance().info() << "  Conditional Receive: " << eigrp.flags.conditionalRecieve << std::endl;
        Logger::getInstance().info() << "  Restart: " << eigrp.flags.restart << std::endl;
        Logger::getInstance().info() << "  End Of Table: " << eigrp.flags.endOfTable << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << eigrp.sequence.toHex() << std::endl;
        Logger::getInstance().info() << "Acknowledgment Number: " << eigrp.ack.toHex() << std::endl;
        Logger::getInstance().info() << "Virtual Router ID: " << eigrp.virtualRouterID.toHex() << std::endl;
        Logger::getInstance().info() << "Autonomous System Number: " << eigrp.autonomousSystem.toHex() << std::endl;

        Logger::getInstance().info() << "Options:" << std::endl;
        for (const auto &option : eigrp.options)
        {
            Logger::getInstance().info() << "  Option: " << option.option.toHex() << std::endl;
            Logger::getInstance().info() << "  Length: " << option.length.toHex() << std::endl;
            Logger::getInstance().info() << "  Value: " << option.value.toHex() << std::endl;
        }
    }

    packetInfo.Layer4.push_back(std::move(eigrp));
    return true;
}

//-----------------------------------------------------------------------------------------------
// Layer 5
//-----------------------------------------------------------------------------------------------

// Parses and processes the DHCP header. bool Packet::decodeDhcp(ByteString &dhcpHeaders)
bool Packet::decodeDhcp(ByteString &dhcpHeaders)
{
    //std::cout << "dhcp " << dhcpHeaders.toHex() << std::endl;
    DhcpHeader dhcp;
    if (!dhcp.decapsulate(dhcpHeaders)) return false;

    if (print)
    {

        Logger::getInstance().info() << "DHCP Frame:" << std::endl;
        Logger::getInstance().info() << "Message Type: " << dhcp.boot.toHex() << std::endl;
        Logger::getInstance().info() << "Hardware Type: " << dhcp.hardwareType.toHex() << std::endl;
        Logger::getInstance().info() << "Hardware Address Length: " << dhcp.hardwareAddressLength.toHex() << std::endl;
        Logger::getInstance().info() << "Hops: " << dhcp.hops.toHex() << std::endl;
        Logger::getInstance().info() << "Transaction ID: " << dhcp.transID.toHex() << std::endl;
        Logger::getInstance().info() << "Seconds Elapsed: " << dhcp.secondsElapsed.toHex() << std::endl;
        Logger::getInstance().info() << "Broadcast: " << dhcp.bootpFlags.broadcast << std::endl;
        Logger::getInstance().info() << "Reserved: " << Functions::byteToHex(dhcp.bootpFlags.reserved) << std::endl;
        Logger::getInstance().info() << "Client IP: " << dhcp.clientIP.toHex() << std::endl;
        Logger::getInstance().info() << "Your Client IP: " << dhcp.yourClientIP.toHex() << std::endl;
        Logger::getInstance().info() << "Next Server Address: " << dhcp.nextServerIP.toHex() << std::endl;
        Logger::getInstance().info() << "Relay Agent IP: " << dhcp.relayAgentIP.toHex() << std::endl;
        Logger::getInstance().info() << "Client MAC Address: " << dhcp.clientMacAddress.toHex() << std::endl;

        Logger::getInstance().info() << "Options:" << std::endl;
        for (const auto &option : dhcp.options)
        {
            Logger::getInstance().info() << "  Option: " << option.option.toHex() << std::endl;
            Logger::getInstance().info() << "  Length: " << option.length.toHex() << std::endl;
            Logger::getInstance().info() << "  Value: " << option.value.toHex() << std::endl;
        }
    }

    packetInfo.Layer5.push_back(std::move(dhcp));
    return true;
}

bool Packet::decodeSysLog(ByteString &syslogHeader)
{
    SyslogHeader syslog;
    size_t priSize{0};
    for (const auto& ch : syslogHeader)
    {
        if (ch != '>')
        {
            priSize++;
        }
        else
        {
            priSize++;
            break;
        }
    }

    syslog.PRI = syslogHeader.substr(0, priSize);
    syslog.message = syslogHeader.substr(priSize);

    if (print) {
        Logger::getInstance().info() << "SysLog Frame:" << std::endl;
        Logger::getInstance().info() << "PRI: " << syslog.PRI << std::endl;
        Logger::getInstance().info() << "Message: " << syslog.message << std::endl;
    }

    return true;
}
