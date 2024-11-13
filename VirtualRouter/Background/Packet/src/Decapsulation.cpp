#include <Decapsulation.h>

// Constructor for Packet class, starts packet inspection.
Packet::Packet(string &packet, bool debug)
{
    print = debug;
    if (print) {Logger::getInstance().info() << Functions::byteToHex(packet) << endl;}
    Inspection(packet);
}

// Inspects the given packet and processes each layer.
void Packet::Inspection(string &packet)
{
    fullPacket = packet;

    start = 0;
    if (print)
    {
        Logger::getInstance().info() << "Layer 2:" << endl;
    }
    L2(packet); // Process Layer 2 (Data Link Layer)
    if (print)
    {
        Logger::getInstance().info() << "Layer 2.5:" << endl;
    }
    L2_5(packet); // Process Layer 2.5 (e.g., VLAN, MPLS)
    if (print)
    {
        Logger::getInstance().info() << "Layer 3:" << endl;
    }
    L3(packet); // Process Layer 3 (Network Layer)
}

// Decapsulates the remaining layers after Layer 3.
void Packet::Decapsulate()
{
    if (print)
    {
        Logger::getInstance().info() << "Layer 4:" << endl;
    }
    L4(fullPacket); // Process Layer 4 (Transport Layer)
    if (print)
    {
        Logger::getInstance().info() << "Layer 5:" << endl;
    }
    L5(fullPacket); // Process Layer 5 (Session Layer)
}

// Handles Layer 2 processing, distinguishing between Ethernet and PPP.
void Packet::L2(string &packet)
{
    if (gre.protocol == variable.gre.ppp)
    {
        string pppHeader = packet.substr(start, 4);
        Ppp(pppHeader); // Process PPP header
        packetInfo.Layer2.push_back(ppp);
    }
    else
    {
        string ethernetHeader = packet.substr(start, 14);
        Ethernet(ethernetHeader); // Process Ethernet header
        packetInfo.Layer2.push_back(ethernet);
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 2.5 headers such as ARP, MPLS, VLAN, or LLDP.
void Packet::L2_5(string &packet)
{
    if (ethernet.type == variable.ethernet.arp)
    {
        string arpHeader = packet.substr(start, 28);
        Arp(arpHeader); // Process ARP header
        packetInfo.Layer2_5.push_back(arp);
    }
    else if (ethernet.type == variable.ethernet.mpls)
    {
        string mplsHeader = Functions::byteToHex(packet.substr(start, 4));
        Mpls(mplsHeader); // Process MPLS header
        packetInfo.Layer2_5.push_back(mpls);
    }
    else if (ethernet.type == variable.ethernet.vlan)
    {
        string vlanHeader = packet.substr(start, 4);
        Vlan(vlanHeader); // Process VLAN header
        packetInfo.Layer2_5.push_back(vlan);
    }
    else if (ethernet.type == variable.ethernet.lldp)
    {
        string lldpHeader = packet.substr(start);
        Lldp(lldpHeader); // Process LLDP header
        packetInfo.Layer2_5.push_back(lldp);
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 3 headers, focusing on IPv4 and its encapsulated protocols.
void Packet::L3(string &packet)
{
    if (ethernet.type == variable.ethernet.ipv4 || ethernet.type == variable.ethernet.mpls)
    {
        int ipv4Size = Functions::binToNum((Functions::byteToBin(packet.substr(start, 1))).substr(4, 4)) * 4;
        string ipv4Header = packet.substr(start, ipv4Size);
        Ipv4(ipv4Header, ipv4Size); // Process IPv4 header
        packetInfo.Layer3.push_back(ipv4);

        if (ipv4.protocol == variable.ipv4.gre)
        {
            string greHeader = packet.substr(start, 12);
            Gre(greHeader); // Process GRE header
            packetInfo.Layer3.push_back(gre);
            if (gre.protocol == variable.gre.ppp)
            {
                string grepacket = packet.substr(start);
                Inspection(grepacket); // Recursively inspect encapsulated GRE packet
                return;
            }
        }
        else if (ipv4.protocol == variable.ipv4.ah)
        {
            int ahSize = (Functions::binToNum(Functions::byteToBin(packet.substr(start + 1, 1))) * 4) + 8;
            string ahHeader = packet.substr(start, ahSize);
            Ah(ahHeader, ahSize); // Process AH header
            packetInfo.Layer3.push_back(ah);
            if (ah.next == variable.ah.esp)
            {
                string espHeader = packet.substr(start);
                Esp(espHeader); // Process ESP header after AH
                packetInfo.Layer3.push_back(esp);
            }
        }
        else if (ipv4.protocol == variable.ah.esp)
        {
            string espHeader = packet.substr(start);
            Esp(espHeader); // Process ESP header
            packetInfo.Layer3.push_back(esp);
        }
        else if (ipv4.protocol == variable.ipv4.icmp)
        {
            string icmpHeader = packet.substr(start, 8);
            Icmp(icmpHeader); // Process ICMP header
            packetInfo.Layer3.push_back(icmp);
        }
        else if (ipv4.protocol == variable.ipv4.igmp)
        {
            string igmpHeader = packet.substr(start);
            Igmp(igmpHeader); // Process IGMP header
            packetInfo.Layer3.push_back(igmp);
        }
        else if (ipv4.protocol == variable.ipv4.eigrp)
        {
            int eigrpSize = Functions::byteToNum(ipv4.totalLength) - 20;
            string eigrpHeader = packet.substr(start, eigrpSize);
            Eigrp(eigrpHeader); // Process EIGRP header
            packetInfo.Layer3.push_back(eigrp);
        }
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 4 headers, including TCP, UDP, and EIGRP.
void Packet::L4(string &packet)
{
    if (ipv4.protocol == variable.ipv4.tcp)
    {
        int tcpSize = Functions::binToNum((Functions::byteToBin(packet.substr(start + 12, 1))).substr(0, 4)) * 4;
        string tcpHeader = packet.substr(start, tcpSize);
        Tcp(tcpHeader, tcpSize); // Process TCP header
        packetInfo.Layer4.push_back(tcp);
    }
    else if (ipv4.protocol == variable.ipv4.udp)
    {
        string udpHeader = packet.substr(start, 8);
        Udp(udpHeader); // Process UDP header
        packetInfo.Layer4.push_back(udp);
    }
    afterPacket = packet.substr(start);
}

// Processes Layer 5 (Session Layer) headers, such as DHCP.
void Packet::L5(string &packet)
{
    if ((udp.sourcePort == variable.udp.dhcp.source && udp.destinationPort == variable.udp.dhcp.destination) ||
        (udp.sourcePort == variable.udp.dhcp.destination && udp.destinationPort == variable.udp.dhcp.source))
    {
        string dhcpHeader = packet.substr(start);
        Dhcp(dhcpHeader); // Process DHCP header
        packetInfo.Layer5.push_back(dhcp);
    }
}

// Parses and processes Ethernet header.
void Packet::Ethernet(string &ethernetHeader)
{

    ethernet.destinationMac = ethernetHeader.substr(0, 6);
    ethernet.sourceMac = ethernetHeader.substr(6, 6);
    ethernet.type = ethernetHeader.substr(12, 2);
    start += 14;

    if (print)
    {

        Logger::getInstance().info() << "Ethernet:" << endl;
        Logger::getInstance().info() << "Destination Mac: " << Functions::byteToHex(ethernet.destinationMac) << endl;
        Logger::getInstance().info() << "Source Mac: " << Functions::byteToHex(ethernet.sourceMac) << endl;
        Logger::getInstance().info() << "Type: " << Functions::byteToHex(ethernet.type) << endl;
    }
}

// Parses and processes ARP header.
void Packet::Arp(string &arpHeader)
{

    arp.hardwareType = arpHeader.substr(0, 2);
    arp.protocolType = arpHeader.substr(2, 2);
    arp.hardwareSize = arpHeader.substr(4, 1);
    arp.protocolSize = arpHeader.substr(5, 1);
    arp.opcode = arpHeader.substr(6, 2);
    arp.senderHardwareAddress = arpHeader.substr(8, 6);
    arp.senderIpAddress = arpHeader.substr(14, 4);
    arp.targetHardwareAddress = arpHeader.substr(18, 6);
    arp.targetIpAddress = arpHeader.substr(24, 4);
    start += 28;

    if (print)
    {
        Logger::getInstance().info() << "ARP Header:" << endl;
        Logger::getInstance().info() << "Hardware Type: " << Functions::byteToHex(arp.hardwareType) << endl;
        Logger::getInstance().info() << "Protocol Type: " << Functions::byteToHex(arp.protocolType) << endl;
        Logger::getInstance().info() << "Hardware Size: " << Functions::byteToHex(arp.hardwareSize) << endl;
        Logger::getInstance().info() << "Protocol Size: " << Functions::byteToHex(arp.protocolSize) << endl;
        Logger::getInstance().info() << "Opcode: " << Functions::byteToHex(arp.opcode) << endl;
        Logger::getInstance().info() << "Sender Hardware Address: " << Functions::byteToHex(arp.senderHardwareAddress) << endl;
        Logger::getInstance().info() << "Sender IP Address: " << Functions::byteToHex(arp.senderIpAddress) << endl;
        Logger::getInstance().info() << "Target Hardware Address: " << Functions::byteToHex(arp.targetHardwareAddress) << endl;
        Logger::getInstance().info() << "Target IP Address: " << Functions::byteToHex(arp.targetIpAddress) << endl;
    }
}

// Parses and processes the IP header.
void Packet::Ipv4(string &ipv4Header, int &ipv4Size)
{

    string ipHeader = Functions::byteToHex(ipv4Header.substr(0, 1));
    ipv4.version = ipHeader.substr(0, 1);
    ipv4.headerLength = ipHeader.substr(1, 1);
    ipv4.serviceField = ipv4Header.substr(1, 1);
    ipv4.totalLength = ipv4Header.substr(2, 2);
    ipv4.identification = ipv4Header.substr(4, 2);
    ipv4.TTL = ipv4Header.substr(8, 1);
    ipv4.protocol = ipv4Header.substr(9, 1);
    ipv4.checksum = ipv4Header.substr(10, 2);
    ipv4.sourceAddress = ipv4Header.substr(12, 4);
    ipv4.destinationAddress = ipv4Header.substr(16, 4);
    start += ipv4Size;

    string fragmentFlag = Functions::byteToBin(ipv4Header.substr(6, 2));

    ipv4.fragmentFlag.reserved = fragmentFlag.substr(0, 1);
    ipv4.fragmentFlag.fragment = fragmentFlag.substr(1, 1);
    ipv4.fragmentFlag.moreFragment = fragmentFlag.substr(2, 1);
    ipv4.fragmentFlag.fragmentOffset = fragmentFlag.substr(3);

    if (print)
    {

        Logger::getInstance().info() << "IPv4 Header:" << endl;
        Logger::getInstance().info() << "Version: " << ipv4.version << endl;
        Logger::getInstance().info() << "Header Length: " << ipv4.headerLength << endl;
        Logger::getInstance().info() << "Service Field: " << Functions::byteToHex(ipv4.serviceField) << endl;
        Logger::getInstance().info() << "Total Length: " << Functions::hexToNum(Functions::byteToHex(ipv4.totalLength)) << " " << this->fullPacket.size() << endl;
        Logger::getInstance().info() << "Identification: " << Functions::byteToHex(ipv4.identification) << endl;
        Logger::getInstance().info() << "TTL: " << Functions::byteToHex(ipv4.TTL) << endl;
        Logger::getInstance().info() << "Protocol: " << Functions::byteToHex(ipv4.protocol) << endl;
        Logger::getInstance().info() << "Checksum: " << Functions::byteToHex(ipv4.checksum) << endl;
        Logger::getInstance().info() << "Source Address: " << Functions::byteAddressToNumAddress(ipv4.sourceAddress) << endl;
        Logger::getInstance().info() << "Destination Address: " << Functions::byteAddressToNumAddress(ipv4.destinationAddress) << endl;
        Logger::getInstance().info() << "Fragment Flags:" << endl;
        Logger::getInstance().info() << "  Reserved: " << ipv4.fragmentFlag.reserved << endl;
        Logger::getInstance().info() << "  Fragment: " << ipv4.fragmentFlag.fragment << endl;
        Logger::getInstance().info() << "  More Fragment: " << ipv4.fragmentFlag.moreFragment << endl;
        Logger::getInstance().info() << "  Fragment Offset: " << ipv4.fragmentFlag.fragmentOffset << endl;
    }

    if (ipv4Size > 20)
    {
        string type = Functions::byteToBin(ipv4Header.substr(20, 1));
        ipv4.options.type.copy = type.substr(0, 1);
        ipv4.options.type.classControl = type.substr(1, 2);
        ipv4.options.type.routerAlert = type.substr(3, 5);
        ipv4.options.length = ipv4Header.substr(21, 1);
        ipv4.options.routerAlert = ipv4Header.substr(22, 1);

        if (print)
        {

            Logger::getInstance().info() << "Options:" << endl;
            Logger::getInstance().info() << "  Type:" << endl;
            Logger::getInstance().info() << "    Copy: " << ipv4.options.type.copy << endl;
            Logger::getInstance().info() << "    Class Control: " << ipv4.options.type.classControl << endl;
            Logger::getInstance().info() << "    Router Alert: " << ipv4.options.type.routerAlert << endl;
            Logger::getInstance().info() << "  Length: " << ipv4.options.length << endl;
            Logger::getInstance().info() << "  Router Alert: " << ipv4.options.routerAlert << endl;
        }
    }
}

// Parses and processes the MPLS header.
void Packet::Mpls(string &mplsHeader)
{

    mpls.label = mplsHeader.substr(0, 5);
    mpls.expBit = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(0, 3);
    mpls.bottomLabelStack = (Functions::hexToBin(mplsHeader.substr(5, 1))).substr(3, 1);
    mpls.TTL = mplsHeader.substr(6, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "MPLS Header:" << endl;
        Logger::getInstance().info() << "Label: " << mpls.label << endl;
        Logger::getInstance().info() << "Exp Bit: " << mpls.expBit << endl;
        Logger::getInstance().info() << "Bottom Label Stack: " << mpls.bottomLabelStack << endl;
        Logger::getInstance().info() << "TTL: " << mpls.TTL << endl;
    }
}

// Parses and processes the TCP header.
void Packet::Tcp(string &tcpHeader, int &tcpSize)
{
    tcp.sourcePort = tcpHeader.substr(0, 2);
    tcp.destinationPort = tcpHeader.substr(2, 2);
    tcp.sequenceNumber = tcpHeader.substr(4, 4);
    tcp.ackNumber = tcpHeader.substr(8, 4);
    tcp.headerLength = tcpHeader.substr(12, 1);
    tcp.windowSize = tcpHeader.substr(14, 2);
    tcp.checksum = tcpHeader.substr(16, 2);
    tcp.urgentPointer = tcpHeader.substr(18, 2);
    start += tcpSize;

    string flags = Functions::byteToBin(tcpHeader.substr(13, 1));

    tcp.flags.congestionWindowReduced = flags.substr(0, 1);
    tcp.flags.ecnEcho = flags.substr(1, 1);
    tcp.flags.urgent = flags.substr(2, 1);
    tcp.flags.acknowledgement = flags.substr(3, 1);
    tcp.flags.push = flags.substr(4, 1);
    tcp.flags.reset = flags.substr(5, 1);
    tcp.flags.syn = flags.substr(6, 1);
    tcp.flags.fin = flags.substr(7, 1);

    if (tcpSize > 20)
    {
        string tcpOptions = tcpHeader.substr(20);
        int optionStart = 0;
        while (optionStart != tcpSize - 20)
        {
            tcpHeader::Option option;
            option.type = tcpOptions.substr(optionStart, 1);
            optionStart += 1;
            if (option.type != std::string("\x01", 1))
            {
                option.length = tcpOptions.substr(optionStart, 1);
                optionStart += 1;
                int valueLength = Functions::byteToNum(option.length) - 2;
                option.value = tcpOptions.substr(optionStart, valueLength);
                optionStart += valueLength;
            }
            tcp.options.push_back(option);
        }
    }

    if (print)
    {

        Logger::getInstance().info() << "TCP Header:" << endl;
        Logger::getInstance().info() << "Source Port: " << Functions::byteToHex(tcp.sourcePort) << endl;
        Logger::getInstance().info() << "Destination Port: " << Functions::byteToHex(tcp.destinationPort) << endl;
        Logger::getInstance().info() << "Sequence Number: " << Functions::byteToHex(tcp.sequenceNumber) << endl;
        Logger::getInstance().info() << "Ack Number: " << Functions::byteToHex(tcp.ackNumber) << endl;
        Logger::getInstance().info() << "Header Length: " << Functions::byteToHex(tcp.headerLength) << endl;
        Logger::getInstance().info() << "Window Size: " << Functions::byteToHex(tcp.windowSize) << endl;
        Logger::getInstance().info() << "Checksum: " << Functions::byteToHex(tcp.checksum) << endl;
        Logger::getInstance().info() << "Urgent Pointer: " << Functions::byteToHex(tcp.urgentPointer) << endl;
        Logger::getInstance().info() << "Flags:" << endl;
        Logger::getInstance().info() << "  Congestion Window Reduced: " << tcp.flags.congestionWindowReduced << endl;
        Logger::getInstance().info() << "  ECN Echo: " << tcp.flags.ecnEcho << endl;
        Logger::getInstance().info() << "  Urgent: " << tcp.flags.urgent << endl;
        Logger::getInstance().info() << "  Acknowledgement: " << tcp.flags.acknowledgement << endl;
        Logger::getInstance().info() << "  Push: " << tcp.flags.push << endl;
        Logger::getInstance().info() << "  Reset: " << tcp.flags.reset << endl;
        Logger::getInstance().info() << "  SYN: " << tcp.flags.syn << endl;
        Logger::getInstance().info() << "  FIN: " << tcp.flags.fin << endl;
    }
}

// Parses and processes the UDP header.
void Packet::Udp(string &udpHeader)
{

    udp.sourcePort = udpHeader.substr(0, 2);
    udp.destinationPort = udpHeader.substr(2, 2);
    udp.length = udpHeader.substr(4, 2);
    udp.checksum = udpHeader.substr(6, 2);
    start += 8;

    if (print)
    {

        Logger::getInstance().info() << "UDP Header:" << endl;
        Logger::getInstance().info() << "Source Port: " << Functions::byteToHex(udp.sourcePort) << endl;
        Logger::getInstance().info() << "Destination Port: " << Functions::byteToHex(udp.destinationPort) << endl;
        Logger::getInstance().info() << "Length: " << Functions::byteToHex(udp.length) << endl;
        Logger::getInstance().info() << "Checksum: " << Functions::byteToHex(udp.checksum) << endl;
    }
}

// Parses and processes the ICMP header.
void Packet::Icmp(string &icmpHeader)
{

    icmp.type = icmpHeader.substr(0, 1);
    icmp.code = icmpHeader.substr(1, 1);
    icmp.checksum = icmpHeader.substr(2, 2);
    icmp.identifier = icmpHeader.substr(4, 2);
    icmp.sequenceNumber = icmpHeader.substr(6, 2);
    start += 8;

    if (print)
    {

        Logger::getInstance().info() << "ICMP Header:" << endl;
        Logger::getInstance().info() << "Type: " << Functions::byteToHex(icmp.type) << endl;
        Logger::getInstance().info() << "Code: " << Functions::byteToHex(icmp.code) << endl;
        Logger::getInstance().info() << "Checksum: " << Functions::byteToHex(icmp.checksum) << endl;
        Logger::getInstance().info() << "Identifier: " << Functions::byteToHex(icmp.identifier) << endl;
        Logger::getInstance().info() << "Sequence Number: " << Functions::byteToHex(icmp.sequenceNumber) << endl;
    }
}

// Parses and processes the IGMP header.
void Packet::Igmp(string &igmpHeader)
{
    //
    //    igmp.type = igmpHeader.substr(0, 1);
    //    igmp.maxRestTime = igmpHeader.substr(1, 1);
    //    igmp.checksum = igmpHeader.substr(2, 2);
    //    igmp.multicastAddress = igmpHeader.substr(4, 4);
    //    start += 8;
    //
    //    if (print) {
    //
    //        Logger::getInstance().info() << "IGMP Header:" << endl;
    //        Logger::getInstance().info() << "Type: " << Functions::byteToHex(igmp.type) << endl;
    //        Logger::getInstance().info() << "Max Rest Time: " << Functions::byteToHex(igmp.maxRestTime) << endl;
    //        Logger::getInstance().info() << "Checksum: " << Functions::byteToHex(igmp.checksum) << endl;
    //        Logger::getInstance().info() << "Multicast Address: " << Functions::byteToHex(igmp.multicastAddress) << endl;
    //
    //    }
    //
    //    if (igmpHeader.size() == 12) {
    //
    //        Logger::getInstance().info() << "imgp" << endl;
    //        igmp.v3.supress = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(5, 1);
    //        Logger::getInstance().info() << "igmp2" << endl;
    //        igmp.v3.qrv = (Functions::byteToBin(igmpHeader.substr(8, 1))).substr(6, 3);
    //        igmp.v3.qqic = igmpHeader.substr(9, 1);
    //        igmp.v3.numSrc = igmpHeader.substr(10, 2);
    //        start += 4;
    //
    //        if (print) {
    //
    //            Logger::getInstance().info() << "IGMP v3:" << endl;
    //            Logger::getInstance().info() << "  Suppress: " << igmp.v3.supress << endl;
    //            Logger::getInstance().info() << "  QRV: " << igmp.v3.qrv << endl;
    //            Logger::getInstance().info() << "  QQIC: " << igmp.v3.qqic << endl;
    //            Logger::getInstance().info() << "  Num Src: " << Functions::byteToHex(igmp.v3.numSrc) << endl;
    //
    //        }
    //    }
}

// Parses and processes the GRE header.
void Packet::Gre(string &greHeader)
{

    string flags = Functions::byteToBin(greHeader.substr(0, 2));
    gre.flags.checksum = flags.substr(0, 1);
    gre.flags.routing = flags.substr(1, 1);
    gre.flags.key = flags.substr(2, 1);
    gre.flags.seqNum = flags.substr(3, 1);
    gre.flags.strictSourceRoute = flags.substr(4, 1);
    gre.flags.recursion = (Functions::byteToBin(greHeader.substr(0, 2))).substr(5, 3);
    gre.flags.acknowledgment = flags.substr(8, 1);
    gre.flags.reserved = (Functions::byteToBin(greHeader.substr(0, 2))).substr(9, 4);
    gre.flags.version = (Functions::byteToBin(greHeader.substr(0, 2))).substr(13, 3);
    gre.protocol = greHeader.substr(2, 2);
    gre.length = greHeader.substr(4, 2);
    gre.callID = greHeader.substr(6, 2);
    gre.seqNum = greHeader.substr(8, 4);
    start += 12;

    if (print)
    {

        Logger::getInstance().info() << "Gre: " << endl;
        Logger::getInstance().info() << "Flags: " << endl;
        Logger::getInstance().info() << "   Checksum: " << gre.flags.checksum << endl;
        Logger::getInstance().info() << "   Routing: " << gre.flags.routing << endl;
        Logger::getInstance().info() << "   Key: " << gre.flags.key << endl;
        Logger::getInstance().info() << "   Sequence Number: " << gre.flags.seqNum << endl;
        Logger::getInstance().info() << "   Strict Source Route: " << gre.flags.strictSourceRoute << endl;
        Logger::getInstance().info() << "   Recursion: " << gre.flags.recursion << endl;
        Logger::getInstance().info() << "   Acknowledgment: " << gre.flags.acknowledgment << endl;
        Logger::getInstance().info() << "   Reserved: " << gre.flags.reserved << endl;
        Logger::getInstance().info() << "   Version: " << gre.flags.version << endl;
        Logger::getInstance().info() << "Protocol: " << Functions::byteToHex(gre.protocol) << endl;
        Logger::getInstance().info() << "Length: " << Functions::byteToHex(gre.length) << endl;
        Logger::getInstance().info() << "Call ID: " << Functions::byteToHex(gre.callID) << endl;
        Logger::getInstance().info() << "Sequence Number: " << Functions::byteToHex(gre.seqNum) << endl;
    }
}

// Parses and processes the PPP header.
void Packet::Ppp(string &pppHeader)
{

    ppp.address = pppHeader.substr(0, 1);
    ppp.control = pppHeader.substr(1, 1);
    ppp.protocol = pppHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "PPP: " << endl;
        Logger::getInstance().info() << "Address: " << Functions::byteToHex(ppp.address) << endl;
        Logger::getInstance().info() << "Control: " << Functions::byteToHex(ppp.control) << endl;
        Logger::getInstance().info() << "Protocol: " << Functions::byteToHex(ppp.protocol) << endl;
    }
}

// Parses and processes the Frame Relay header.
void Packet::Frame(string &frameHeader)
{

    string relay = Functions::byteToBin(frameHeader.substr(0, 1));
    frame.firstAddress.cr = relay.substr(6, 1);
    frame.firstAddress.ea = relay.substr(7, 1);
    relay = Functions::byteToBin(frameHeader.substr(1, 1));
    frame.secondAddress.fecn = relay.substr(4, 1);
    frame.secondAddress.becn = relay.substr(5, 1);
    frame.secondAddress.de = relay.substr(6, 1);
    frame.secondAddress.ea = relay.substr(7, 1);
    frame.type = frameHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "Frame Relay:" << endl;
        Logger::getInstance().info() << "First DLCI: " << frame.firstAddress.dlci << endl;
        Logger::getInstance().info() << "First CR: " << frame.firstAddress.cr << endl;
        Logger::getInstance().info() << "First EA: " << frame.firstAddress.ea << endl;
        Logger::getInstance().info() << "Second DLCI: " << frame.secondAddress.dlci << endl;
        Logger::getInstance().info() << "Second FECN: " << frame.secondAddress.fecn << endl;
        Logger::getInstance().info() << "Second BECN: " << frame.secondAddress.becn << endl;
        Logger::getInstance().info() << "Second DE: " << frame.secondAddress.de << endl;
        Logger::getInstance().info() << "Second EA: " << frame.secondAddress.ea << endl;
        Logger::getInstance().info() << "Type: " << Functions::byteToHex(frame.type) << endl;
    }
}

// Parses and processes the AH header.
void Packet::Ah(string &ahHeader, int &ahSize)
{

    ah.next = ahHeader.substr(0, 1);
    ah.length = ahHeader.substr(1, 1);
    ah.reserved = ahHeader.substr(2, 2);
    ah.spi = ahHeader.substr(4, 4);
    ah.sequence = ahHeader.substr(8, 4);
    ah.icv = ahHeader.substr(12);
    start += ahSize;

    if (print)
    {

        Logger::getInstance().info() << "AH:" << endl;
        Logger::getInstance().info() << "Next: " << Functions::byteToHex(ah.next) << endl;
        Logger::getInstance().info() << "Length: " << Functions::byteToHex(ah.length) << endl;
        Logger::getInstance().info() << "Reserved: " << Functions::byteToHex(ah.reserved) << endl;
        Logger::getInstance().info() << "AH SPI: " << Functions::byteToHex(ah.spi) << endl;
        Logger::getInstance().info() << "AH Sequence: " << Functions::byteToHex(ah.sequence) << endl;
        Logger::getInstance().info() << "AH ICV: " << Functions::byteToHex(ah.icv) << endl;
    }
}

// Parses and processes the ESP header.
void Packet::Esp(string &espHeader)
{

    esp.spi = espHeader.substr(0, 4);
    esp.sequence = espHeader.substr(4, 4);
    start = +8;

    if (print)
    {

        Logger::getInstance().info() << "ESP: " << endl;
        Logger::getInstance().info() << "ESP SPI: " << Functions::byteToHex(esp.spi) << endl;
        Logger::getInstance().info() << "ESP Sequence: " << Functions::byteToHex(esp.sequence) << endl;
    }
}

// Parses and processes the VLAN header.
void Packet::Vlan(string &vlanHeader)
{

    string vlans = Functions::byteToBin(vlanHeader.substr(0, 2));
    vlan.priority = vlans.substr(0, 3);
    vlan.dei = vlans.substr(3, 1);
    vlan.type = vlanHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        Logger::getInstance().info() << "Vlan: " << endl;
        Logger::getInstance().info() << "Priority: " << vlan.priority << endl;
        Logger::getInstance().info() << "DEI: " << vlan.dei << endl;
        Logger::getInstance().info() << "ID: " << vlan.id << endl;
        Logger::getInstance().info() << "Type: " << Functions::byteToHex(vlan.type) << endl;
    }
}

// Parses and processes the LLDP header.
void Packet::Lldp(string &lldpHeader)
{

    uint16_t pos = 0;
    while (pos < lldpHeader.size())
    {

        lldpHeader::TLV tlv;

        uint16_t typeLength = (lldpHeader[pos] << 8) | lldpHeader[pos + 1];
        string sec = Functions::byteToBin(lldpHeader.substr(pos, 2));
        tlv.type = Functions::binToNum(sec.substr(0, 7));
        tlv.length = Functions::binToNum(sec.substr(7, 9));
        pos += 2;

        tlv.value = lldpHeader.substr(pos, tlv.length);
        pos += tlv.length;

        switch (tlv.type)
        {
        case 1:
            lldp.chassisID = tlv;
            break;
        case 2:
            lldp.portID = tlv;
            break;
        case 3:
            lldp.ttl = tlv;
            break;
        case 4:
            lldp.portDescription = tlv;
            break;
        case 5:
            lldp.systemName = tlv;
            break;
        case 6:
            lldp.systemDescription = tlv;
            break;
        case 7:
            lldp.systemCapabilities = tlv;
            break;
        case 8:
            lldp.managementAddress = tlv;
            break;
        case 127:
            lldp.organizationallySpecific = tlv;
            break;
        case 0:
            lldp.endOfLLDPDU = tlv;
            break;
        default:
            break;
        }
        if (tlv.type == 0)
        {
            break;
        }
    }

    if (print)
    {

        Logger::getInstance().info() << "LLDP Frame:" << endl;
        Logger::getInstance().info() << "Chassis ID: " << lldp.chassisID.value << endl;
        Logger::getInstance().info() << "Port ID: " << lldp.portID.value << endl;
        Logger::getInstance().info() << "TTL: " << lldp.ttl.value << endl;
        Logger::getInstance().info() << "Port Description: " << lldp.portDescription.value << endl;
        Logger::getInstance().info() << "System Name: " << lldp.systemName.value << endl;
        Logger::getInstance().info() << "System Description: " << lldp.systemDescription.value << endl;
        Logger::getInstance().info() << "System Capabilities: " << lldp.systemCapabilities.value << endl;
        Logger::getInstance().info() << "Management Address: " << lldp.managementAddress.value << endl;
        Logger::getInstance().info() << "Organizationally Specific: " << lldp.organizationallySpecific.value << endl;
        Logger::getInstance().info() << "End of LLDPDU" << endl;
    }
}

// Parses and processes the DHCP header.
void Packet::Dhcp(string &dhcpHeader)
{
    dhcp.boot = dhcpHeader.substr(0, 1);
    dhcp.hardwareType = dhcpHeader.substr(1, 1);
    dhcp.hardwareAddressLength = dhcpHeader.substr(2, 1);
    dhcp.hops = dhcpHeader.substr(3, 1);
    dhcp.transID = dhcpHeader.substr(4, 4);
    dhcp.secondsElapsed = dhcpHeader.substr(8, 2);
    string flags = Functions::byteToBin(dhcpHeader.substr(10, 2));
    dhcp.bootpFlags.broadcast = flags.substr(0, 1);
    dhcp.bootpFlags.reserved = flags.substr(1, 7);
    dhcp.clientIP = dhcpHeader.substr(12, 4);
    dhcp.yourClientIP = dhcpHeader.substr(16, 4);
    dhcp.nextServerIP = dhcpHeader.substr(20, 4);
    dhcp.relayAgentIP = dhcpHeader.substr(24, 4);
    dhcp.clientMacAddress = dhcpHeader.substr(28, 6);
    dhcp.clientHardwareAddressPadding = dhcpHeader.substr(34, 10);
    dhcp.serverHostName = dhcpHeader.substr(44, 64);
    dhcp.bootFile = dhcpHeader.substr(108, 128);
    dhcp.magicCookie = dhcpHeader.substr(236, 4);
    int dhcpStart = 240;
    start = dhcpStart;

    int dhcpEnd{};
    int dhcpLength = dhcpHeader.size();
    for (int i = dhcpLength - 1; i >= 0; --i)
    {
        if (dhcpHeader[i] == variable.dhcp.end[0])
        {
            dhcpEnd = i;
            break;
        }
    }

    while (dhcpStart != dhcpEnd)
    {
        dhcpHeader::Option option;
        option.option = dhcpHeader.substr(dhcpStart, 1);
        dhcpStart += 1;
        option.length = dhcpHeader.substr(dhcpStart, 1);
        dhcpStart += 1;
        option.value = dhcpHeader.substr(dhcpStart, Functions::byteToNum(option.length));
        int dhcpADD = Functions::byteToNum(option.length);
        dhcpStart += dhcpADD;
        start += dhcpADD;
        dhcp.options.push_back(option);
    }

    dhcp.end = dhcpHeader.substr(dhcpStart, 1);
    dhcp.padding = dhcpHeader.substr(dhcpStart + 1);

    if (print)
    {

        Logger::getInstance().info() << "DHCP Frame:" << endl;
        Logger::getInstance().info() << "Message Type: " << Functions::byteToHex(dhcp.boot) << endl;
        Logger::getInstance().info() << "Hardware Type: " << Functions::byteToHex(dhcp.hardwareType) << endl;
        Logger::getInstance().info() << "Hardware Address Length: " << Functions::byteToHex(dhcp.hardwareAddressLength) << endl;
        Logger::getInstance().info() << "Hops: " << Functions::byteToHex(dhcp.hops) << endl;
        Logger::getInstance().info() << "Transaction ID: " << Functions::byteToHex(dhcp.transID) << endl;
        Logger::getInstance().info() << "Seconds Elapsed: " << Functions::byteToHex(dhcp.secondsElapsed) << endl;
        Logger::getInstance().info() << "Broadcast: " << dhcp.bootpFlags.broadcast << endl;
        Logger::getInstance().info() << "Reserved: " << Functions::byteToHex(dhcp.bootpFlags.reserved) << endl;
        Logger::getInstance().info() << "Client IP: " << Functions::byteToHex(dhcp.clientIP) << endl;
        Logger::getInstance().info() << "Your Client IP: " << Functions::byteToHex(dhcp.yourClientIP) << endl;
        Logger::getInstance().info() << "Next Server Address: " << Functions::byteToHex(dhcp.nextServerIP) << endl;
        Logger::getInstance().info() << "Relay Agent IP: " << Functions::byteToHex(dhcp.relayAgentIP) << endl;
        Logger::getInstance().info() << "Client MAC Address: " << Functions::byteToHex(dhcp.clientMacAddress) << endl;

        Logger::getInstance().info() << "Options:" << endl;
        for (const auto &option : eigrp.options)
        {
            Logger::getInstance().info() << "  Option: " << Functions::byteToHex(option.option) << endl;
            Logger::getInstance().info() << "  Length: " << Functions::byteToHex(option.length) << endl;
            Logger::getInstance().info() << "  Value: " << Functions::byteToHex(option.value) << endl;
        }
    }
}

// Parses and processes the EIGRP header.
void Packet::Eigrp(string &eigrpHeader)
{
    eigrp.version = eigrpHeader.substr(0, 1);
    eigrp.opcode = eigrpHeader.substr(1, 1);
    eigrp.checksum = eigrpHeader.substr(2, 2);
    string flag = Functions::byteToBin(eigrpHeader.substr(4, 4));
    eigrp.flags.init = flag.substr(28, 1);
    eigrp.flags.conditionalRecieve = flag.substr(29, 1);
    eigrp.flags.restart = flag.substr(30, 1);
    eigrp.flags.endOfTable = flag.substr(31, 1);
    eigrp.sequence = eigrpHeader.substr(8, 4);
    eigrp.ack = eigrpHeader.substr(12, 4);
    eigrp.virtualRouterID = eigrpHeader.substr(16, 2);
    eigrp.autonomousSystem = eigrpHeader.substr(18, 2);
    int eigrpStart = 20;
    start += eigrpStart;
    int eigrpEnd = eigrpHeader.size();

    while (eigrpStart != eigrpEnd)
    {
        eigrpHeader::Option option;
        option.option = eigrpHeader.substr(eigrpStart, 2);
        eigrpStart += 2;
        option.length = eigrpHeader.substr(eigrpStart, 2);
        eigrpStart += 2;
        int eigrpADD = Functions::byteToNum(option.length) - 4;
        option.value = eigrpHeader.substr(eigrpStart, eigrpADD);
        eigrpStart += eigrpADD;
        start += eigrpADD;
        eigrp.options.push_back(option);
    }

    if (print)
    {
        Logger::getInstance().info() << "EIGRP Frame:" << endl;
        Logger::getInstance().info() << "Version: " << Functions::byteToHex(eigrp.version) << endl;
        Logger::getInstance().info() << "Opcode: " << Functions::byteToHex(eigrp.opcode) << endl;
        Logger::getInstance().info() << "Checksum: " << Functions::byteToHex(eigrp.checksum) << endl;
        Logger::getInstance().info() << "Flags: " << endl;
        Logger::getInstance().info() << "  Init: " << eigrp.flags.init << endl;
        Logger::getInstance().info() << "  Conditional Receive: " << eigrp.flags.conditionalRecieve << endl;
        Logger::getInstance().info() << "  Restart: " << eigrp.flags.restart << endl;
        Logger::getInstance().info() << "  End Of Table: " << eigrp.flags.endOfTable << endl;
        Logger::getInstance().info() << "Sequence Number: " << Functions::byteToHex(eigrp.sequence) << endl;
        Logger::getInstance().info() << "Acknowledgment Number: " << Functions::byteToHex(eigrp.ack) << endl;
        Logger::getInstance().info() << "Virtual Router ID: " << Functions::byteToHex(eigrp.virtualRouterID) << endl;
        Logger::getInstance().info() << "Autonomous System Number: " << Functions::byteToHex(eigrp.autonomousSystem) << endl;

        Logger::getInstance().info() << "Options:" << endl;
        for (const auto &option : eigrp.options)
        {
            Logger::getInstance().info() << "  Option: " << Functions::byteToHex(option.option) << endl;
            Logger::getInstance().info() << "  Length: " << Functions::byteToHex(option.length) << endl;
            Logger::getInstance().info() << "  Value: " << Functions::byteToHex(option.value) << endl;
        }
    }
}

void Packet::SysLog(string &syslogHeader)
{
    int priSize{0};
    for (const char& ch : syslogHeader)
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
        Logger::getInstance().info() << "SysLog Frame:" << endl;
        Logger::getInstance().info() << "PRI: " << syslog.PRI << endl;
        Logger::getInstance().info() << "Message: " << syslog.message << endl;
    }
}
