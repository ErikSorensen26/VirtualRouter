#include <Decapsulation.h>

// Constructor for Packet class, starts packet inspection
Packet::Packet(ByteString &packet, bool debug, Interface& iface) : currentInterface(iface)
{
    print = debug;
    if (print) {Logger::getInstance().info() << packet.toHex() << std::endl;}
    inspection(packet);
}

// Inspects the given packet and processes each layer.
void Packet::inspection(ByteString &packet)
{
    fullPacket = packet;

    start = 0;
    if (print)
    {
        Logger::getInstance().info() << "Layer 2:" << std::endl;
    }
    l2(packet); // Process Layer 2 (Data Link Layer)
    if (print)
    {
        Logger::getInstance().info() << "Layer 2.5:" << std::endl;
    }
    l2_5(packet); // Process Layer 2.5 (e.g., VLAN, MPLS)
    if (print)
    {
        Logger::getInstance().info() << "Layer 3:" << std::endl;
    }
    l3(packet); // Process Layer 3 (Network Layer)
}

// Decapsulates the remaining layers after Layer 3.
void Packet::decapsulate()
{
    if (print)
    {
        Logger::getInstance().info() << "Layer 4:" << std::endl;
    }
    l4(fullPacket); // Process Layer 4 (Transport Layer)
    if (print)
    {
        Logger::getInstance().info() << "Layer 5:" << std::endl;
    }
    l5(fullPacket); // Process Layer 5 (Session Layer)
}

// Handles Layer 2 processing, distinguishing between Ethernet and PPP.
void Packet::l2(ByteString &packet)
{
    if (gre && gre->protocol == Variable::Gre::ppp)
    {
        ByteString pppHeader = packet.substr(start, 4);
        decodePpp(pppHeader); // Process PPP header
        packetInfo.Layer2.push_back(ppp);
    }
    else
    {
        ByteString ethernetHeader = packet.substr(start, 14);
        decodeEthernet(ethernetHeader); // Process Ethernet header
        packetInfo.Layer2.push_back(ethernet);
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 2.5 headers such as ARP, MPLS, VLAN, or LLDP.
void Packet::l2_5(ByteString &packet)
{
    if (ethernet && ethernet->type == Variable::Ethernet::arp)
    {
        ByteString arpHeader = packet.substr(start, 28);
        decodeArp(arpHeader); // Process ARP header
        packetInfo.Layer2_5.push_back(arp);
    }
    else if (ethernet && ethernet->type == Variable::Ethernet::mpls)
    {
        ByteString mplsHeader = packet.substr(start, 4).toHex();
        decodeMpls(mplsHeader); // Process MPLS header
        packetInfo.Layer2_5.push_back(mpls);
    }
    else if (ethernet && ethernet->type == Variable::Ethernet::vlan)
    {
        ByteString vlanHeader = packet.substr(start, 4);
        decodeVlan(vlanHeader); // Process VLAN header
        packetInfo.Layer2_5.push_back(vlan);
    }
    else if (ethernet && ethernet->type == Variable::Ethernet::lldp)
    {
        ByteString lldpHeader = packet.substr(start);
        decodeLldp(lldpHeader); // Process LLDP header
        packetInfo.Layer2_5.push_back(lldp);
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 3 headers, focusing on IPv4/IPv6 and its encapsulated protocols.
void Packet::l3(ByteString &packet)
{
    if (ethernet && (ethernet->type == Variable::Ethernet::ipv4 || ethernet->type == Variable::Ethernet::mpls))
    {
        size_t ipv4Size = Functions::binToNum((Functions::byteToBin(packet.substr(start, 1))).substr(4, 4)) * 4;
        ByteString ipv4Header = packet.substr(start, ipv4Size);
        decodeIPv4(ipv4Header, ipv4Size); // Process IPv4 header
        packetInfo.Layer3.push_back(ipv4);

        if (ipv4 && ipv4->protocol == Variable::IP::gre)
        {
            ByteString greHeader = packet.substr(start, 12);
            decodeGre(greHeader); // Process GRE header
            packetInfo.Layer3.push_back(gre);
            if (gre && gre->protocol == Variable::Gre::ppp)
            {
                ByteString grepacket = packet.substr(start);
                inspection(grepacket); // Recursively inspect encapsulated GRE packet
                return;
            }
        }
        else if (ipv4 && ipv4->protocol == Variable::IP::ah)
        {
            size_t ahSize = (Functions::binToNum(Functions::byteToBin(packet.substr(start + 1, 1))) * 4) + 8;
            ByteString ahHeader = packet.substr(start, ahSize);
            decodeAh(ahHeader, ahSize); // Process AH header
            packetInfo.Layer3.push_back(ah);
            if (ah && ah->next == Variable::Ah::esp)
            {
                ByteString espHeader = packet.substr(start);
                decodeEsp(espHeader); // Process ESP header after AH
                packetInfo.Layer3.push_back(esp);
            }
        }
        else if (ipv4 && ipv4->protocol == Variable::Ah::esp)
        {
            ByteString espHeader = packet.substr(start);
            decodeEsp(espHeader); // Process ESP header
            packetInfo.Layer3.push_back(esp);
        }
        else if (ipv4 && ipv4->protocol == Variable::IP::icmp)
        {
            ByteString icmpHeader = packet.substr(start, 8);
            decodeIcmp(icmpHeader); // Process ICMP header
            packetInfo.Layer3.push_back(icmp);
        }
        else if (ipv4 && ipv4->protocol == Variable::IP::igmp)
        {
            ByteString igmpHeader = packet.substr(start);
            decodeIgmp(igmpHeader); // Process IGMP header
            packetInfo.Layer3.push_back(igmp);
        }
        else if (ipv4 && ipv4->protocol == Variable::IP::eigrp)
        {
            size_t eigrpSize = static_cast<size_t>(Functions::byteToNum(ipv4->totalLength) - 20);
            ByteString eigrpHeader = packet.substr(start, eigrpSize);
            decodeEigrp(eigrpHeader); // Process EIGRP header
            packetInfo.Layer3.push_back(eigrp);
        }
    }
    if (ethernet->type == Variable::Ethernet::ipv6)
    {
        size_t ipv6Size = static_cast<size_t>(ipv6Size = 40);
        ByteString ipv6Header = packet.substr(start, ipv6Size);
        decodeIPv6(ipv6Header); // Process IPv6 header
        packetInfo.Layer3.push_back(ipv6);

        if (ipv6 && ipv6->protocol == Variable::IP::eigrp)
        {
            size_t eigrpSize = static_cast<size_t>(Functions::byteToNum(ipv6->payloadLength) - 40);
            ByteString eigrpHeader = packet.substr(start, eigrpSize);
            decodeEigrp(eigrpHeader); // Process EigrpHeader
            packetInfo.Layer3.push_back(eigrp);
        }
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 4 headers, including TCP, UDP, and EIGRP.
void Packet::l4(ByteString &packet)
{
    if (ipv4 && ipv4->protocol == Variable::IP::tcp)
    {
        size_t tcpSize = Functions::binToNum((Functions::byteToBin(packet.substr(start + 12, 1))).substr(0, 4)) * 4;
        ByteString tcpHeader = packet.substr(start, tcpSize);
        decodeTcp(tcpHeader, tcpSize); // Process TCP header
        packetInfo.Layer4.push_back(tcp);
    }
    else if (ipv4 && ipv4->protocol == Variable::IP::udp)
    {
        ByteString udpHeader = packet.substr(start, 8);
        decodeUdp(udpHeader); // Process UDP header
        packetInfo.Layer4.push_back(udp);
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 5 (Session Layer) headers, such as DHCP.
void Packet::l5(ByteString &packet)
{
    if (udp && ((udp->sourcePort == Variable::Udp::Dhcp::source && udp->destinationPort == Variable::Udp::Dhcp::destination) ||
        (udp->sourcePort == Variable::Udp::Dhcp::destination && udp->destinationPort == Variable::Udp::Dhcp::source)))
    {
        ByteString dhcpHeader = packet.substr(start);
        decodeDhcp(dhcpHeader); // Process DHCP header
        packetInfo.Layer5.push_back(dhcp);
    }
}

// Parses and processes Ethernet header.
void Packet::decodeEthernet(ByteString &ethernetHeader)
{
    ethernet = std::make_shared<EthernetHeader>();
    ethernet->destinationMac = ethernetHeader.substr(0, 6);
    ethernet->sourceMac = ethernetHeader.substr(6, 6);
    ethernet->type = ethernetHeader.substr(12, 2);
    start += 14;

    ByteString currentMac;
    {
        std::shared_lock<std::shared_mutex> lock(currentInterface.Get()->ipMutex);
        currentMac = currentInterface.Get()->macAddress;
    }

    if (currentMac == ethernet->sourceMac)
    {
        Logger::getInstance().info() << "Packet dropped due to receiving current MAC" << std::endl;
        return;
    }

    if (print)
    {

        Logger::getInstance().info() << "Ethernet:" << std::endl;
        Logger::getInstance().info() << "Destination Mac: " << ethernet->destinationMac.toHex() << std::endl;
        Logger::getInstance().info() << "Source Mac: " << ethernet->sourceMac.toHex() << std::endl;
        Logger::getInstance().info() << "Type: " << ethernet->type.toHex() << std::endl;
    }
}

// Parses and processes ARP header.
void Packet::decodeArp(ByteString &arpHeader)
{
    arp = std::make_shared<ArpHeader>();
    arp->hardwareType = arpHeader.substr(0, 2);
    arp->protocolType = arpHeader.substr(2, 2);
    arp->hardwareSize = arpHeader.substr(4, 1);
    arp->protocolSize = arpHeader.substr(5, 1);
    arp->opcode = arpHeader.substr(6, 2);
    arp->senderHardwareAddress = arpHeader.substr(8, 6);
    arp->senderIpAddress = arpHeader.substr(14, 4);
    arp->targetHardwareAddress = arpHeader.substr(18, 6);
    arp->targetIpAddress = arpHeader.substr(24, 4);
    start += 28;

    if (print)
    {
        Logger::getInstance().info() << "ARP Header:" << std::endl;
        Logger::getInstance().info() << "Hardware Type: " << arp->hardwareType.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol Type: " << arp->protocolType.toHex() << std::endl;
        Logger::getInstance().info() << "Hardware Size: " << arp->hardwareSize.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol Size: " << arp->protocolSize.toHex() << std::endl;
        Logger::getInstance().info() << "Opcode: " << arp->opcode.toHex() << std::endl;
        Logger::getInstance().info() << "Sender Hardware Address: " << arp->senderHardwareAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Sender IP Address: " << arp->senderIpAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Target Hardware Address: " << arp->targetHardwareAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Target IP Address: " << arp->targetIpAddress.toHex() << std::endl;
    }
}

// Parses and processes the IP header.
void Packet::decodeIPv4(ByteString &ipv4Header, size_t &ipv4Size)
{
    ipv4 = std::make_shared<IPv4Header>();
    ByteString ipHeader = ipv4Header.substr(0, 1).toHex();
    ipv4->version = ipHeader.substr(0, 1);
    ipv4->headerLength = ipHeader.substr(1, 1);
    ipv4->serviceField = ipv4Header.substr(1, 1);
    ipv4->totalLength = ipv4Header.substr(2, 2);
    ipv4->identification = ipv4Header.substr(4, 2);
    ipv4->TTL = ipv4Header.substr(8, 1);
    ipv4->protocol = ipv4Header.substr(9, 1);
    ipv4->checksum = ipv4Header.substr(10, 2);
    ipv4->sourceAddress = ipv4Header.substr(12, 4);
    ipv4->destinationAddress = ipv4Header.substr(16, 4);
    start += ipv4Size;

    ByteString fragmentFlag = Functions::byteToBin(ipv4Header.substr(6, 2));

    ipv4->fragmentFlag.reserved = fragmentFlag.substr(0, 1);
    ipv4->fragmentFlag.fragment = fragmentFlag.substr(1, 1);
    ipv4->fragmentFlag.moreFragment = fragmentFlag.substr(2, 1);
    ipv4->fragmentFlag.fragmentOffset = fragmentFlag.substr(3);

    if (print)
    {

        Logger::getInstance().info() << "IPv4 Header:" << std::endl;
        Logger::getInstance().info() << "Version: " << ipv4->version << std::endl;
        Logger::getInstance().info() << "Header Length: " << ipv4->headerLength << std::endl;
        Logger::getInstance().info() << "Service Field: " << ipv4->serviceField.toHex() << std::endl;
        Logger::getInstance().info() << "Total Length: " << Functions::hexToNum(ipv4->totalLength.toHex()) << " " << this->fullPacket.size() << std::endl;
        Logger::getInstance().info() << "Identification: " << ipv4->identification.toHex() << std::endl;
        Logger::getInstance().info() << "TTL: " << ipv4->TTL.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol: " << ipv4->protocol.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << ipv4->checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Source Address: " << Functions::byteAddressToNumAddress(ipv4->sourceAddress) << std::endl;
        Logger::getInstance().info() << "Destination Address: " << Functions::byteAddressToNumAddress(ipv4->destinationAddress) << std::endl;
        Logger::getInstance().info() << "Fragment Flags:" << std::endl;
        Logger::getInstance().info() << "  Reserved: " << ipv4->fragmentFlag.reserved << std::endl;
        Logger::getInstance().info() << "  Fragment: " << ipv4->fragmentFlag.fragment << std::endl;
        Logger::getInstance().info() << "  More Fragment: " << ipv4->fragmentFlag.moreFragment << std::endl;
        Logger::getInstance().info() << "  Fragment Offset: " << ipv4->fragmentFlag.fragmentOffset << std::endl;
    }

    if (ipv4Size > 20)
    {
        ByteString type = Functions::byteToBin(ipv4Header.substr(20, 1));
        ipv4->options.type.copy = type.substr(0, 1);
        ipv4->options.type.classControl = type.substr(1, 2);
        ipv4->options.type.routerAlert = type.substr(3, 5);
        ipv4->options.length = ipv4Header.substr(21, 1);
        ipv4->options.routerAlert = ipv4Header.substr(22, 1);

        if (print)
        {

            Logger::getInstance().info() << "Options:" << std::endl;
            Logger::getInstance().info() << "  Type:" << std::endl;
            Logger::getInstance().info() << "    Copy: " << ipv4->options.type.copy << std::endl;
            Logger::getInstance().info() << "    Class Control: " << ipv4->options.type.classControl << std::endl;
            Logger::getInstance().info() << "    Router Alert: " << ipv4->options.type.routerAlert << std::endl;
            Logger::getInstance().info() << "  Length: " << ipv4->options.length << std::endl;
            Logger::getInstance().info() << "  Router Alert: " << ipv4->options.routerAlert << std::endl;
        }
    }
}

// Parses and processes the IPv6 header
void Packet::decodeIPv6(ByteString &ipv6Header)
{
    ipv6 = std::make_shared<IPv6Header>();
    ByteString ipv6Temp = Functions::byteToHex(ipv6Header.substr(0, 4));
    ipv6->version = ipv6Temp.substr(0, 1);
    ipv6->trafficClass = ipv6Temp.substr(1, 2);
    ipv6->flowLabel = ipv6Temp.substr(3, 5);
    ipv6->payloadLength = ipv6Header.substr(4, 2);
    ipv6->protocol = ipv6Header.substr(6, 1);
    ipv6->hopLimit = ipv6Header.substr(7, 1);
    ipv6->sourceAddress = ipv6Header.substr(8, 16);
    ipv6->destinationAddress = ipv6Header.substr(24, 16);
    start += 40;

    if (print)
    {
        Logger::getInstance().info() << "IPv6 Header:" << std::endl;
        Logger::getInstance().info() << "Version: " << ipv6->version.toHex() << std::endl;
        Logger::getInstance().info() << "Flow Label: " << Functions::byteToBin(ipv6->flowLabel) << std::endl;
        Logger::getInstance().info() << "Payload Length: " << Functions::byteToNum(ipv6->payloadLength) << std::endl;
        Logger::getInstance().info() << "Protocol: " << ipv6->protocol.toHex() << std::endl;
        Logger::getInstance().info() << "Hop Limit: " << Functions::byteToNum(ipv6->hopLimit) << std::endl;
        Logger::getInstance().info() << "Source Address: " << ipv6->sourceAddress.toHex() << std::endl;
        Logger::getInstance().info() << "Destination Address: " << ipv6->destinationAddress.toHex() << std::endl;
    }
}

// Parses and processes the MPLS header.
void Packet::decodeMpls(ByteString &mplsHeader)
{
    mpls = std::make_shared<MplsHeader>();
    mpls->label = mplsHeader.substr(0, 5);
    mpls->expBit = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(0, 3);
    mpls->bottomLabelStack = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(3, 1);
    mpls->TTL = mplsHeader.substr(6, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "MPLS Header:" << std::endl;
        Logger::getInstance().info() << "Label: " << mpls->label << std::endl;
        Logger::getInstance().info() << "Exp Bit: " << mpls->expBit << std::endl;
        Logger::getInstance().info() << "Bottom Label Stack: " << mpls->bottomLabelStack << std::endl;
        Logger::getInstance().info() << "TTL: " << mpls->TTL << std::endl;
    }
}

// Parses and processes the TCP header.
void Packet::decodeTcp(ByteString &tcpHeader, size_t &tcpSize)
{
    tcp = std::make_shared<TcpHeader>();
    tcp->sourcePort = tcpHeader.substr(0, 2);
    tcp->destinationPort = tcpHeader.substr(2, 2);
    tcp->sequenceNumber = tcpHeader.substr(4, 4);
    tcp->ackNumber = tcpHeader.substr(8, 4);
    tcp->headerLength = tcpHeader.substr(12, 1);
    tcp->windowSize = tcpHeader.substr(14, 2);
    tcp->checksum = tcpHeader.substr(16, 2);
    tcp->urgentPointer = tcpHeader.substr(18, 2);
    start += tcpSize;

    ByteString flags = Functions::byteToBin(tcpHeader.substr(13, 1));

    tcp->flags.congestionWindowReduced = flags.substr(0, 1);
    tcp->flags.ecnEcho = flags.substr(1, 1);
    tcp->flags.urgent = flags.substr(2, 1);
    tcp->flags.acknowledgement = flags.substr(3, 1);
    tcp->flags.push = flags.substr(4, 1);
    tcp->flags.reset = flags.substr(5, 1);
    tcp->flags.syn = flags.substr(6, 1);
    tcp->flags.fin = flags.substr(7, 1);

    if (tcpSize > 20)
    {
        ByteString tcpOptions = tcpHeader.substr(20);
        size_t optionStart = 0;
        while (optionStart != tcpSize - 20)
        {
            TcpHeader::Option option;
            option.type = tcpOptions.substr(optionStart, 1);
            optionStart += 1;
            if (option.type != std::string("\x01", 1))
            {
                option.length = tcpOptions.substr(optionStart, 1);
                optionStart += 1;
                size_t valueLength = static_cast<size_t>(Functions::byteToNum(option.length) - 2);
                option.value = tcpOptions.substr(optionStart, valueLength);
                optionStart += valueLength;
            }
            tcp->options.push_back(option);
        }
    }

    if (print)
    {

        Logger::getInstance().info() << "TCP Header:" << std::endl;
        Logger::getInstance().info() << "Source Port: " << tcp->sourcePort.toHex() << std::endl;
        Logger::getInstance().info() << "Destination Port: " << tcp->destinationPort.toHex() << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << tcp->sequenceNumber.toHex() << std::endl;
        Logger::getInstance().info() << "Ack Number: " << tcp->ackNumber.toHex() << std::endl;
        Logger::getInstance().info() << "Header Length: " << tcp->headerLength.toHex() << std::endl;
        Logger::getInstance().info() << "Window Size: " << tcp->windowSize.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << tcp->checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Urgent Pointer: " << tcp->urgentPointer.toHex() << std::endl;
        Logger::getInstance().info() << "Flags:" << std::endl;
        Logger::getInstance().info() << "  Congestion Window Reduced: " << tcp->flags.congestionWindowReduced << std::endl;
        Logger::getInstance().info() << "  ECN Echo: " << tcp->flags.ecnEcho << std::endl;
        Logger::getInstance().info() << "  Urgent: " << tcp->flags.urgent << std::endl;
        Logger::getInstance().info() << "  Acknowledgement: " << tcp->flags.acknowledgement << std::endl;
        Logger::getInstance().info() << "  Push: " << tcp->flags.push << std::endl;
        Logger::getInstance().info() << "  Reset: " << tcp->flags.reset << std::endl;
        Logger::getInstance().info() << "  SYN: " << tcp->flags.syn << std::endl;
        Logger::getInstance().info() << "  FIN: " << tcp->flags.fin << std::endl;
    }
}

// Parses and processes the UDP header.
void Packet::decodeUdp(ByteString &udpHeader)
{
    udp = std::make_shared<UdpHeader>();
    udp->sourcePort = udpHeader.substr(0, 2);
    udp->destinationPort = udpHeader.substr(2, 2);
    udp->length = udpHeader.substr(4, 2);
    udp->checksum = udpHeader.substr(6, 2);
    start += 8;

    if (print)
    {

        Logger::getInstance().info() << "UDP Header:" << std::endl;
        Logger::getInstance().info() << "Source Port: " << udp->sourcePort.toHex() << std::endl;
        Logger::getInstance().info() << "Destination Port: " << udp->destinationPort.toHex() << std::endl;
        Logger::getInstance().info() << "Length: " << udp->length.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << udp->checksum.toHex() << std::endl;
    }
}

// Parses and processes the ICMP header.
void Packet::decodeIcmp(ByteString &icmpHeader)
{
    icmp = std::make_shared<IcmpHeader>();
    icmp->type = icmpHeader.substr(0, 1);
    icmp->code = icmpHeader.substr(1, 1);
    icmp->checksum = icmpHeader.substr(2, 2);
    icmp->identifier = icmpHeader.substr(4, 2);
    icmp->sequenceNumber = icmpHeader.substr(6, 2);
    start += 8;

    if (print)
    {

        Logger::getInstance().info() << "ICMP Header:" << std::endl;
        Logger::getInstance().info() << "Type: " << icmp->type.toHex() << std::endl;
        Logger::getInstance().info() << "Code: " << icmp->code.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << icmp->checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Identifier: " << icmp->identifier.toHex() << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << icmp->sequenceNumber.toHex() << std::endl;
    }
}

// Parses and processes the ICMPv6 header
void Packet::decodeIcmpV6(ByteString &icmpV6Header)
{
    icmpv6 = std::make_shared<IcmpV6Header>();
    icmpv6->type = icmpV6Header.substr(0, 1);
    icmpv6->code = icmpV6Header.substr(1, 1);
    icmpv6->checksum = icmpV6Header.substr(2, 2);
    icmpv6->reserved = icmpV6Header.substr(4, 4);
    size_t icmpv6Start = 8;
    start += icmpv6Start;
    size_t icmpv6End = icmpV6Header.size();
    

    while (icmpv6Start != icmpv6End)
    {
        IcmpV6Header::Option option;
        option.option = icmpV6Header.substr(icmpv6Start, 2);
        icmpv6Start += 2;
        option.length = icmpV6Header.substr(icmpv6Start, 2);
        icmpv6Start += 2;
        size_t icmpv6ADD = static_cast<size_t>(Functions::byteToNum(option.length) - 4);
        option.value = icmpV6Header.substr(icmpv6Start, icmpv6ADD);
        icmpv6Start += icmpv6ADD;
        start += icmpv6ADD;
        icmpv6->options.push_back(option);
    }
}

// Parses and processes the IGMP header.
void Packet::decodeIgmp(ByteString &igmpHeader)
{
    igmp = std::make_shared<IgmpHeader>();
    //
    //    igmp->type = igmpHeader.substr(0, 1);
    //    igmp->maxRestTime = igmpHeader.substr(1, 1);
    //    igmp->checksum = igmpHeader.substr(2, 2);
    //    igmp->multicastAddress = igmpHeader.substr(4, 4);
    //    start += 8;
    //
    //    if (print) {
    //
    //        Logger::getInstance().info() << "IGMP Header:" << std::endl;
    //        Logger::getInstance().info() << "Type: " << igmp->type.toHex() << std::endl;
    //        Logger::getInstance().info() << "Max Rest Time: " << igmp->maxRestTime.toHex() << std::endl;
    //        Logger::getInstance().info() << "Checksum: " << igmp->checksum.toHex() << std::endl;
    //        Logger::getInstance().info() << "Multicast Address: " << igmp->multicastAddress.toHex() << std::endl;
    //
    //    }
    //
    //    if (igmpHeader.size() == 12) {
    //
    //        Logger::getInstance().info() << "imgp" << std::endl;
    //        igmp->v3.supress = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(5, 1);
    //        Logger::getInstance().info() << "igmp2" << std::endl;
    //        igmp->v3.qrv = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(6, 3);
    //        igmp->v3.qqic = igmpHeader.substr(9, 1);
    //        igmp->v3.numSrc = igmpHeader.substr(10, 2);
    //        start += 4;
    //
    //        if (print) {
    //
    //            Logger::getInstance().info() << "IGMP v3:" << std::endl;
    //            Logger::getInstance().info() << "  Suppress: " << igmp->v3.supress << std::endl;
    //            Logger::getInstance().info() << "  QRV: " << igmp->v3.qrv << std::endl;
    //            Logger::getInstance().info() << "  QQIC: " << igmp->v3.qqic << std::endl;
    //            Logger::getInstance().info() << "  Num Src: " << Functions::byteToHex(igmp->v3.numSrc) << std::endl;
    //
    //        }
    //    }
}

// Parses and processes the GRE header.
void Packet::decodeGre(ByteString &greHeader)
{
    gre = std::make_shared<GreHeade>();
    ByteString flags = Functions::byteToBin(greHeader.substr(0, 2));
    gre->flags.checksum = flags.substr(0, 1);
    gre->flags.routing = flags.substr(1, 1);
    gre->flags.key = flags.substr(2, 1);
    gre->flags.seqNum = flags.substr(3, 1);
    gre->flags.strictSourceRoute = flags.substr(4, 1);
    gre->flags.recursion = (Functions::byteToBin(greHeader.substr(0, 2))).substr(5, 3);
    gre->flags.acknowledgment = flags.substr(8, 1);
    gre->flags.reserved = (Functions::byteToBin(greHeader.substr(0, 2))).substr(9, 4);
    gre->flags.version = (Functions::byteToBin(greHeader.substr(0, 2))).substr(13, 3);
    gre->protocol = greHeader.substr(2, 2);
    gre->length = greHeader.substr(4, 2);
    gre->callID = greHeader.substr(6, 2);
    gre->seqNum = greHeader.substr(8, 4);
    start += 12;

    if (print)
    {

        Logger::getInstance().info() << "Gre: " << std::endl;
        Logger::getInstance().info() << "Flags: " << std::endl;
        Logger::getInstance().info() << "   Checksum: " << gre->flags.checksum << std::endl;
        Logger::getInstance().info() << "   Routing: " << gre->flags.routing << std::endl;
        Logger::getInstance().info() << "   Key: " << gre->flags.key << std::endl;
        Logger::getInstance().info() << "   Sequence Number: " << gre->flags.seqNum << std::endl;
        Logger::getInstance().info() << "   Strict Source Route: " << gre->flags.strictSourceRoute << std::endl;
        Logger::getInstance().info() << "   Recursion: " << gre->flags.recursion << std::endl;
        Logger::getInstance().info() << "   Acknowledgment: " << gre->flags.acknowledgment << std::endl;
        Logger::getInstance().info() << "   Reserved: " << gre->flags.reserved << std::endl;
        Logger::getInstance().info() << "   Version: " << gre->flags.version << std::endl;
        Logger::getInstance().info() << "Protocol: " << gre->protocol.toHex() << std::endl;
        Logger::getInstance().info() << "Length: " << gre->length.toHex() << std::endl;
        Logger::getInstance().info() << "Call ID: " << gre->callID.toHex() << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << gre->seqNum.toHex() << std::endl;
    }
}

// Parses and processes the PPP header.
void Packet::decodePpp(ByteString &pppHeader)
{
    ppp = std::make_shared<PppHeader>();
    ppp->address = pppHeader.substr(0, 1);
    ppp->control = pppHeader.substr(1, 1);
    ppp->protocol = pppHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "PPP: " << std::endl;
        Logger::getInstance().info() << "Address: " << ppp->address.toHex() << std::endl;
        Logger::getInstance().info() << "Control: " << ppp->control.toHex() << std::endl;
        Logger::getInstance().info() << "Protocol: " << ppp->protocol.toHex() << std::endl;
    }
}

// Parses and processes the Frame Relay header.
void Packet::decodeFrame(ByteString &frameHeader)
{
    frame = std::make_shared<FrameHeader>();
    ByteString relay = Functions::byteToBin(frameHeader.substr(0, 1));
    frame->firstAddress.cr = relay.substr(6, 1);
    frame->firstAddress.ea = relay.substr(7, 1);
    relay = Functions::byteToBin(frameHeader.substr(1, 1));
    frame->secondAddress.fecn = relay.substr(4, 1);
    frame->secondAddress.becn = relay.substr(5, 1);
    frame->secondAddress.de = relay.substr(6, 1);
    frame->secondAddress.ea = relay.substr(7, 1);
    frame->type = frameHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "Frame Relay:" << std::endl;
        Logger::getInstance().info() << "First DLCI: " << frame->firstAddress.dlci << std::endl;
        Logger::getInstance().info() << "First CR: " << frame->firstAddress.cr << std::endl;
        Logger::getInstance().info() << "First EA: " << frame->firstAddress.ea << std::endl;
        Logger::getInstance().info() << "Second DLCI: " << frame->secondAddress.dlci << std::endl;
        Logger::getInstance().info() << "Second FECN: " << frame->secondAddress.fecn << std::endl;
        Logger::getInstance().info() << "Second BECN: " << frame->secondAddress.becn << std::endl;
        Logger::getInstance().info() << "Second DE: " << frame->secondAddress.de << std::endl;
        Logger::getInstance().info() << "Second EA: " << frame->secondAddress.ea << std::endl;
        Logger::getInstance().info() << "Type: " << frame->type.toHex() << std::endl;
    }
}

// Parses and processes the AH header.
void Packet::decodeAh(ByteString &ahHeader, size_t &ahSize)
{
    ah = std::make_shared<AhHeader>();
    ah->next = ahHeader.substr(0, 1);
    ah->length = ahHeader.substr(1, 1);
    ah->reserved = ahHeader.substr(2, 2);
    ah->spi = ahHeader.substr(4, 4);
    ah->sequence = ahHeader.substr(8, 4);
    ah->icv = ahHeader.substr(12);
    start += ahSize;

    if (print)
    {

        Logger::getInstance().info() << "AH:" << std::endl;
        Logger::getInstance().info() << "Next: " << ah->next.toHex() << std::endl;
        Logger::getInstance().info() << "Length: " << ah->length.toHex() << std::endl;
        Logger::getInstance().info() << "Reserved: " << ah->reserved.toHex() << std::endl;
        Logger::getInstance().info() << "AH SPI: " << ah->spi.toHex() << std::endl;
        Logger::getInstance().info() << "AH Sequence: " << ah->sequence.toHex() << std::endl;
        Logger::getInstance().info() << "AH ICV: " << ah->icv.toHex() << std::endl;
    }
}

// Parses and processes the ESP header.
void Packet::decodeEsp(ByteString &espHeader)
{
    esp = std::make_shared<EspHeader>();
    esp->spi = espHeader.substr(0, 4);
    esp->sequence = espHeader.substr(4, 4);
    start = +8;

    if (print)
    {

        Logger::getInstance().info() << "ESP: " << std::endl;
        Logger::getInstance().info() << "ESP SPI: " << esp->spi.toHex() << std::endl;
        Logger::getInstance().info() << "ESP Sequence: " << esp->sequence.toHex() << std::endl;
    }
}

// Parses and processes the VLAN header.
void Packet::decodeVlan(ByteString &vlanHeader)
{
    vlan = std::make_shared<VlanHeader>();
    ByteString vlans = Functions::byteToBin(vlanHeader.substr(0, 2));
    vlan->priority = vlans.substr(0, 3);
    vlan->dei = vlans.substr(3, 1);
    vlan->type = vlanHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "Vlan: " << std::endl;
        Logger::getInstance().info() << "Priority: " << vlan->priority << std::endl;
        Logger::getInstance().info() << "DEI: " << vlan->dei << std::endl;
        Logger::getInstance().info() << "ID: " << vlan->id << std::endl;
        Logger::getInstance().info() << "Type: " << vlan->type.toHex() << std::endl;
    }
}

// Parses and processes the LLDP header.
void Packet::decodeLldp(ByteString &lldpHeader)
{
    lldp = std::make_shared<LldpHeader>();
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
}

// Parses and processes the DHCP header.
void Packet::decodeDhcp(ByteString &dhcpHeaders)
{
    dhcp = std::make_shared<DhcpHeader>();
    ByteString dhcpHeader = dhcpHeaders;
    dhcp->boot = dhcpHeader.substr(0, 1);
    dhcp->hardwareType = dhcpHeader.substr(1, 1);
    dhcp->hardwareAddressLength = dhcpHeader.substr(2, 1);
    dhcp->hops = dhcpHeader.substr(3, 1);
    dhcp->transID = dhcpHeader.substr(4, 4);
    dhcp->secondsElapsed = dhcpHeader.substr(8, 2);
    ByteString flags = Functions::byteToBin(dhcpHeader.substr(10, 2));
    dhcp->bootpFlags.broadcast = flags.substr(0, 1);
    dhcp->bootpFlags.reserved = flags.substr(1, 7);
    dhcp->clientIP = dhcpHeader.substr(12, 4);
    dhcp->yourClientIP = dhcpHeader.substr(16, 4);
    dhcp->nextServerIP = dhcpHeader.substr(20, 4);
    dhcp->relayAgentIP = dhcpHeader.substr(24, 4);
    dhcp->clientMacAddress = dhcpHeader.substr(28, 6);
    dhcp->clientHardwareAddressPadding = dhcpHeader.substr(34, 10);
    dhcp->serverHostName = dhcpHeader.substr(44, 64);
    dhcp->bootFile = dhcpHeader.substr(108, 128);
    dhcp->magicCookie = dhcpHeader.substr(236, 4);
    size_t dhcpStart = 240;
    start = dhcpStart;

    size_t dhcpEnd{};
    size_t dhcpLength = dhcpHeader.size();
    for (size_t i = dhcpLength - 1; i >= 0; --i)
    {
        if (dhcpHeader[i] == Variable::Dhcp::end[0])
        {
            dhcpEnd = i;
            break;
        }
    }

    while (dhcpStart != dhcpEnd)
    {
        DhcpHeader::Option option;
        option.option = dhcpHeader.substr(dhcpStart, 1);
        dhcpStart += 1;
        option.length = dhcpHeader.substr(dhcpStart, 1);
        dhcpStart += 1;
        option.value = dhcpHeader.substr(dhcpStart, static_cast<size_t>(Functions::byteToNum(option.length)));
        size_t dhcpADD = static_cast<size_t>(Functions::byteToNum(option.length));
        dhcpStart += dhcpADD;
        start += dhcpADD;
        dhcp->options.push_back(option);
    }

    dhcp->end = dhcpHeader.substr(dhcpStart, 1);
    dhcp->padding = dhcpHeader.substr(dhcpStart + 1);

    if (print)
    {

        Logger::getInstance().info() << "DHCP Frame:" << std::endl;
        Logger::getInstance().info() << "Message Type: " << dhcp->boot.toHex() << std::endl;
        Logger::getInstance().info() << "Hardware Type: " << dhcp->hardwareType.toHex() << std::endl;
        Logger::getInstance().info() << "Hardware Address Length: " << dhcp->hardwareAddressLength.toHex() << std::endl;
        Logger::getInstance().info() << "Hops: " << dhcp->hops.toHex() << std::endl;
        Logger::getInstance().info() << "Transaction ID: " << dhcp->transID.toHex() << std::endl;
        Logger::getInstance().info() << "Seconds Elapsed: " << dhcp->secondsElapsed.toHex() << std::endl;
        Logger::getInstance().info() << "Broadcast: " << dhcp->bootpFlags.broadcast << std::endl;
        Logger::getInstance().info() << "Reserved: " << Functions::byteToHex(dhcp->bootpFlags.reserved) << std::endl;
        Logger::getInstance().info() << "Client IP: " << dhcp->clientIP.toHex() << std::endl;
        Logger::getInstance().info() << "Your Client IP: " << dhcp->yourClientIP.toHex() << std::endl;
        Logger::getInstance().info() << "Next Server Address: " << dhcp->nextServerIP.toHex() << std::endl;
        Logger::getInstance().info() << "Relay Agent IP: " << dhcp->relayAgentIP.toHex() << std::endl;
        Logger::getInstance().info() << "Client MAC Address: " << dhcp->clientMacAddress.toHex() << std::endl;

        Logger::getInstance().info() << "Options:" << std::endl;
        for (const auto &option : dhcp->options)
        {
            Logger::getInstance().info() << "  Option: " << option.option.toHex() << std::endl;
            Logger::getInstance().info() << "  Length: " << option.length.toHex() << std::endl;
            Logger::getInstance().info() << "  Value: " << option.value.toHex() << std::endl;
        }
    }
}

// Parses and processes the EIGRP header.
void Packet::decodeEigrp(ByteString &eigrpHeader)
{
    eigrp = std::make_shared<EigrpHeader>();
    eigrp->version = eigrpHeader.substr(0, 1);
    eigrp->opcode = eigrpHeader.substr(1, 1);
    eigrp->checksum = eigrpHeader.substr(2, 2);
    ByteString flag = Functions::byteToBin(eigrpHeader.substr(4, 4));
    eigrp->flags.endOfTable = flag.substr(28, 1);
    eigrp->flags.restart = flag.substr(29, 1);
    eigrp->flags.conditionalRecieve = flag.substr(30, 1);
    eigrp->flags.init = flag.substr(31, 1);
    eigrp->sequence = eigrpHeader.substr(8, 4);
    eigrp->ack = eigrpHeader.substr(12, 4);
    eigrp->virtualRouterID = eigrpHeader.substr(16, 2);
    eigrp->autonomousSystem = eigrpHeader.substr(18, 2);
    size_t eigrpStart = 20;
    start += eigrpStart;
    size_t eigrpEnd = eigrpHeader.size();

    while (eigrpStart != eigrpEnd)
    {
        EigrpHeader::Option option;
        option.option = eigrpHeader.substr(eigrpStart, 2);
        eigrpStart += 2;
        option.length = eigrpHeader.substr(eigrpStart, 2);
        eigrpStart += 2;
        size_t eigrpADD = static_cast<size_t>(Functions::byteToNum(option.length) - 4);
        option.value = eigrpHeader.substr(eigrpStart, eigrpADD);
        eigrpStart += eigrpADD;
        start += eigrpADD;
        eigrp->options.push_back(option);
    }

    if (print)
    {
        Logger::getInstance().info() << "EIGRP Frame:" << std::endl;
        Logger::getInstance().info() << "Version: " << eigrp->version.toHex() << std::endl;
        Logger::getInstance().info() << "Opcode: " << eigrp->opcode.toHex() << std::endl;
        Logger::getInstance().info() << "Checksum: " << eigrp->checksum.toHex() << std::endl;
        Logger::getInstance().info() << "Flags: " << std::endl;
        Logger::getInstance().info() << "  Init: " << eigrp->flags.init << std::endl;
        Logger::getInstance().info() << "  Conditional Receive: " << eigrp->flags.conditionalRecieve << std::endl;
        Logger::getInstance().info() << "  Restart: " << eigrp->flags.restart << std::endl;
        Logger::getInstance().info() << "  End Of Table: " << eigrp->flags.endOfTable << std::endl;
        Logger::getInstance().info() << "Sequence Number: " << eigrp->sequence.toHex() << std::endl;
        Logger::getInstance().info() << "Acknowledgment Number: " << eigrp->ack.toHex() << std::endl;
        Logger::getInstance().info() << "Virtual Router ID: " << eigrp->virtualRouterID.toHex() << std::endl;
        Logger::getInstance().info() << "Autonomous System Number: " << eigrp->autonomousSystem.toHex() << std::endl;

        Logger::getInstance().info() << "Options:" << std::endl;
        for (const auto &option : eigrp->options)
        {
            Logger::getInstance().info() << "  Option: " << option.option.toHex() << std::endl;
            Logger::getInstance().info() << "  Length: " << option.length.toHex() << std::endl;
            Logger::getInstance().info() << "  Value: " << option.value.toHex() << std::endl;
        }
    }
}

void Packet::decodeSysLog(ByteString &syslogHeader)
{
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

    syslog->PRI = syslogHeader.substr(0, priSize);
    syslog->message = syslogHeader.substr(priSize);

    if (print) {
        Logger::getInstance().info() << "SysLog Frame:" << std::endl;
        Logger::getInstance().info() << "PRI: " << syslog->PRI << std::endl;
        Logger::getInstance().info() << "Message: " << syslog->message << std::endl;
    }
}
