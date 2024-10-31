#include <Decapsulation.h>

// Constructor for Packet class, starts packet inspection.
Packet::Packet(string &packet)
{
    //print = true;
    if (print) {cout << function->byteToHex(packet) << endl;}
    Inspection(packet);
}

// Inspects the given packet and processes each layer.
void Packet::Inspection(string &packet)
{
    fullPacket = packet;

    start = 0;
    if (print)
    {
        cout << "Layer 2:" << endl;
    }
    L2(packet); // Process Layer 2 (Data Link Layer)
    if (print)
    {
        cout << "Layer 2.5:" << endl;
    }
    L2_5(packet); // Process Layer 2.5 (e.g., VLAN, MPLS)
    if (print)
    {
        cout << "Layer 3:" << endl;
    }
    L3(packet); // Process Layer 3 (Network Layer)
}

// Decapsulates the remaining layers after Layer 3.
void Packet::Decapsulate()
{
    if (print)
    {
        cout << "Layer 4:" << endl;
    }
    L4(fullPacket); // Process Layer 4 (Transport Layer)
    if (print)
    {
        cout << "Layer 5:" << endl;
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
        string mplsHeader = function->byteToHex(packet.substr(start, 4));
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
        int ipv4Size = function->binToDec((function->byteToBin(packet.substr(start, 1))).substr(4, 4)) * 4;
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
            int ahSize = (function->binToDec(function->byteToBin(packet.substr(start + 1, 1))) * 4) + 8;
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
            int eigrpSize = function->hexToNum(function->byteToHex(ipv4.totalLength)) - 20;
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
        int tcpSize = function->binToDec((function->byteToBin(packet.substr(start + 12, 1))).substr(0, 4)) * 4;
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
    if ((function->byteToHex(udp.sourcePort) == "0044" && function->byteToHex(udp.destinationPort) == "0043") ||
        (function->byteToHex(udp.sourcePort) == "0043" && function->byteToHex(udp.destinationPort) == "0044"))
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

        cout << "Ethernet:" << endl;
        cout << "Destination Mac: " << function->byteToHex(ethernet.destinationMac) << endl;
        cout << "Source Mac: " << function->byteToHex(ethernet.sourceMac) << endl;
        cout << "Type: " << function->byteToHex(ethernet.type) << endl;
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
        cout << "ARP Header:" << endl;
        cout << "Hardware Type: " << function->byteToHex(arp.hardwareType) << endl;
        cout << "Protocol Type: " << function->byteToHex(arp.protocolType) << endl;
        cout << "Hardware Size: " << function->byteToHex(arp.hardwareSize) << endl;
        cout << "Protocol Size: " << function->byteToHex(arp.protocolSize) << endl;
        cout << "Opcode: " << function->byteToHex(arp.opcode) << endl;
        cout << "Sender Hardware Address: " << function->byteToHex(arp.senderHardwareAddress) << endl;
        cout << "Sender IP Address: " << function->byteToHex(arp.senderIpAddress) << endl;
        cout << "Target Hardware Address: " << function->byteToHex(arp.targetHardwareAddress) << endl;
        cout << "Target IP Address: " << function->byteToHex(arp.targetIpAddress) << endl;
    }
}

// Parses and processes the IP header.
void Packet::Ipv4(string &ipv4Header, int &ipv4Size)
{

    string ipHeader = function->byteToHex(ipv4Header.substr(0, 1));
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

    string fragmentFlag = function->byteToBin(ipv4Header.substr(6, 2));

    ipv4.fragmentFlag.reserved = fragmentFlag.substr(0, 1);
    ipv4.fragmentFlag.fragment = fragmentFlag.substr(1, 1);
    ipv4.fragmentFlag.moreFragment = fragmentFlag.substr(2, 1);
    ipv4.fragmentFlag.fragmentOffset = fragmentFlag.substr(3);

    if (print)
    {

        cout << "IPv4 Header:" << endl;
        cout << "Version: " << ipv4.version << endl;
        cout << "Header Length: " << ipv4.headerLength << endl;
        cout << "Service Field: " << function->byteToHex(ipv4.serviceField) << endl;
        cout << "Total Length: " << function->hexToNum(function->byteToHex(ipv4.totalLength)) << " " << this->fullPacket.size() << endl;
        cout << "Identification: " << function->byteToHex(ipv4.identification) << endl;
        cout << "TTL: " << function->byteToHex(ipv4.TTL) << endl;
        cout << "Protocol: " << function->byteToHex(ipv4.protocol) << endl;
        cout << "Checksum: " << function->byteToHex(ipv4.checksum) << endl;
        cout << "Source Address: " << function->getIPAddress(ipv4.sourceAddress) << endl;
        cout << "Destination Address: " << function->getIPAddress(ipv4.destinationAddress) << endl;
        cout << "Fragment Flags:" << endl;
        cout << "  Reserved: " << ipv4.fragmentFlag.reserved << endl;
        cout << "  Fragment: " << ipv4.fragmentFlag.fragment << endl;
        cout << "  More Fragment: " << ipv4.fragmentFlag.moreFragment << endl;
        cout << "  Fragment Offset: " << ipv4.fragmentFlag.fragmentOffset << endl;
    }

    if (ipv4Size > 20)
    {
        string type = function->byteToBin(ipv4Header.substr(20, 1));
        ipv4.options.type.copy = type.substr(0, 1);
        ipv4.options.type.classControl = type.substr(1, 2);
        ipv4.options.type.routerAlert = type.substr(3, 5);
        ipv4.options.length = ipv4Header.substr(21, 1);
        ipv4.options.routerAlert = ipv4Header.substr(22, 1);

        if (print)
        {

            cout << "Options:" << endl;
            cout << "  Type:" << endl;
            cout << "    Copy: " << ipv4.options.type.copy << endl;
            cout << "    Class Control: " << ipv4.options.type.classControl << endl;
            cout << "    Router Alert: " << ipv4.options.type.routerAlert << endl;
            cout << "  Length: " << ipv4.options.length << endl;
            cout << "  Router Alert: " << ipv4.options.routerAlert << endl;
        }
    }
}

// Parses and processes the MPLS header.
void Packet::Mpls(string &mplsHeader)
{

    mpls.label = mplsHeader.substr(0, 5);
    mpls.expBit = (function->hexToBin(mplsHeader.substr(5, 1))).substr(0, 3);
    mpls.bottomLabelStack = (function->hexToBin(mplsHeader.substr(5, 1))).substr(3, 1);
    mpls.TTL = mplsHeader.substr(6, 2);
    start += 4;

    if (print)
    {

        cout << "MPLS Header:" << endl;
        cout << "Label: " << mpls.label << endl;
        cout << "Exp Bit: " << mpls.expBit << endl;
        cout << "Bottom Label Stack: " << mpls.bottomLabelStack << endl;
        cout << "TTL: " << mpls.TTL << endl;
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

    string flags = function->byteToBin(tcpHeader.substr(13, 1));

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
            if (function->byteToHex(option.type) != "01")
            {
                option.length = tcpOptions.substr(optionStart, 1);
                optionStart += 1;
                int valueLength = (function->hexToNum(function->byteToHex(option.length))) - 2;
                option.value = tcpOptions.substr(optionStart, valueLength);
                optionStart += valueLength;
            }
            tcp.options.push_back(option);
        }
    }

    if (print)
    {

        cout << "TCP Header:" << endl;
        cout << "Source Port: " << function->byteToHex(tcp.sourcePort) << endl;
        cout << "Destination Port: " << function->byteToHex(tcp.destinationPort) << endl;
        cout << "Sequence Number: " << function->byteToHex(tcp.sequenceNumber) << endl;
        cout << "Ack Number: " << function->byteToHex(tcp.ackNumber) << endl;
        cout << "Header Length: " << function->byteToHex(tcp.headerLength) << endl;
        cout << "Window Size: " << function->byteToHex(tcp.windowSize) << endl;
        cout << "Checksum: " << function->byteToHex(tcp.checksum) << endl;
        cout << "Urgent Pointer: " << function->byteToHex(tcp.urgentPointer) << endl;
        cout << "Flags:" << endl;
        cout << "  Congestion Window Reduced: " << tcp.flags.congestionWindowReduced << endl;
        cout << "  ECN Echo: " << tcp.flags.ecnEcho << endl;
        cout << "  Urgent: " << tcp.flags.urgent << endl;
        cout << "  Acknowledgement: " << tcp.flags.acknowledgement << endl;
        cout << "  Push: " << tcp.flags.push << endl;
        cout << "  Reset: " << tcp.flags.reset << endl;
        cout << "  SYN: " << tcp.flags.syn << endl;
        cout << "  FIN: " << tcp.flags.fin << endl;
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

        cout << "UDP Header:" << endl;
        cout << "Source Port: " << function->byteToHex(udp.sourcePort) << endl;
        cout << "Destination Port: " << function->byteToHex(udp.destinationPort) << endl;
        cout << "Length: " << function->byteToHex(udp.length) << endl;
        cout << "Checksum: " << function->byteToHex(udp.checksum) << endl;
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

        cout << "ICMP Header:" << endl;
        cout << "Type: " << function->byteToHex(icmp.type) << endl;
        cout << "Code: " << function->byteToHex(icmp.code) << endl;
        cout << "Checksum: " << function->byteToHex(icmp.checksum) << endl;
        cout << "Identifier: " << function->byteToHex(icmp.identifier) << endl;
        cout << "Sequence Number: " << function->byteToHex(icmp.sequenceNumber) << endl;
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
    //        cout << "IGMP Header:" << endl;
    //        cout << "Type: " << function->byteToHex(igmp.type) << endl;
    //        cout << "Max Rest Time: " << function->byteToHex(igmp.maxRestTime) << endl;
    //        cout << "Checksum: " << function->byteToHex(igmp.checksum) << endl;
    //        cout << "Multicast Address: " << function->byteToHex(igmp.multicastAddress) << endl;
    //
    //    }
    //
    //    if (igmpHeader.size() == 12) {
    //
    //        cout << "imgp" << endl;
    //        igmp.v3.supress = (function->byteToBin(igmpHeader.substr(8, 1))).substr(5, 1);
    //        cout << "igmp2" << endl;
    //        igmp.v3.qrv = (function->byteToBin(igmpHeader.substr(8, 1))).substr(6, 3);
    //        igmp.v3.qqic = igmpHeader.substr(9, 1);
    //        igmp.v3.numSrc = igmpHeader.substr(10, 2);
    //        start += 4;
    //
    //        if (print) {
    //
    //            cout << "IGMP v3:" << endl;
    //            cout << "  Suppress: " << igmp.v3.supress << endl;
    //            cout << "  QRV: " << igmp.v3.qrv << endl;
    //            cout << "  QQIC: " << igmp.v3.qqic << endl;
    //            cout << "  Num Src: " << function->byteToHex(igmp.v3.numSrc) << endl;
    //
    //        }
    //    }
}

// Parses and processes the GRE header.
void Packet::Gre(string &greHeader)
{

    string flags = function->byteToBin(greHeader.substr(0, 2));
    gre.flags.checksum = flags.substr(0, 1);
    gre.flags.routing = flags.substr(1, 1);
    gre.flags.key = flags.substr(2, 1);
    gre.flags.seqNum = flags.substr(3, 1);
    gre.flags.strictSourceRoute = flags.substr(4, 1);
    gre.flags.recursion = (function->byteToBin(greHeader.substr(0, 2))).substr(5, 3);
    gre.flags.acknowledgment = flags.substr(8, 1);
    gre.flags.reserved = (function->byteToBin(greHeader.substr(0, 2))).substr(9, 4);
    gre.flags.version = (function->byteToBin(greHeader.substr(0, 2))).substr(13, 3);
    gre.protocol = greHeader.substr(2, 2);
    gre.length = greHeader.substr(4, 2);
    gre.callID = greHeader.substr(6, 2);
    gre.seqNum = greHeader.substr(8, 4);
    start += 12;

    if (print)
    {

        cout << "Gre: " << endl;
        cout << "Flags: " << endl;
        cout << "   Checksum: " << gre.flags.checksum << endl;
        cout << "   Routing: " << gre.flags.routing << endl;
        cout << "   Key: " << gre.flags.key << endl;
        cout << "   Sequence Number: " << gre.flags.seqNum << endl;
        cout << "   Strict Source Route: " << gre.flags.strictSourceRoute << endl;
        cout << "   Recursion: " << gre.flags.recursion << endl;
        cout << "   Acknowledgment: " << gre.flags.acknowledgment << endl;
        cout << "   Reserved: " << gre.flags.reserved << endl;
        cout << "   Version: " << gre.flags.version << endl;
        cout << "Protocol: " << function->byteToHex(gre.protocol) << endl;
        cout << "Length: " << function->byteToHex(gre.length) << endl;
        cout << "Call ID: " << function->byteToHex(gre.callID) << endl;
        cout << "Sequence Number: " << function->byteToHex(gre.seqNum) << endl;
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

        cout << "PPP: " << endl;
        cout << "Address: " << function->byteToHex(ppp.address) << endl;
        cout << "Control: " << function->byteToHex(ppp.control) << endl;
        cout << "Protocol: " << function->byteToHex(ppp.protocol) << endl;
    }
}

// Parses and processes the Frame Relay header.
void Packet::Frame(string &frameHeader)
{

    string relay = function->byteToBin(frameHeader.substr(0, 1));
    frame.firstAddress.cr = relay.substr(6, 1);
    frame.firstAddress.ea = relay.substr(7, 1);
    relay = function->byteToBin(frameHeader.substr(1, 1));
    frame.secondAddress.fecn = relay.substr(4, 1);
    frame.secondAddress.becn = relay.substr(5, 1);
    frame.secondAddress.de = relay.substr(6, 1);
    frame.secondAddress.ea = relay.substr(7, 1);
    frame.type = frameHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        cout << "Frame Relay:" << endl;
        cout << "First DLCI: " << frame.firstAddress.dlci << endl;
        cout << "First CR: " << frame.firstAddress.cr << endl;
        cout << "First EA: " << frame.firstAddress.ea << endl;
        cout << "Second DLCI: " << frame.secondAddress.dlci << endl;
        cout << "Second FECN: " << frame.secondAddress.fecn << endl;
        cout << "Second BECN: " << frame.secondAddress.becn << endl;
        cout << "Second DE: " << frame.secondAddress.de << endl;
        cout << "Second EA: " << frame.secondAddress.ea << endl;
        cout << "Type: " << function->byteToHex(frame.type) << endl;
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

        cout << "AH:" << endl;
        cout << "Next: " << function->byteToHex(ah.next) << endl;
        cout << "Length: " << function->byteToHex(ah.length) << endl;
        cout << "Reserved: " << function->byteToHex(ah.reserved) << endl;
        cout << "AH SPI: " << function->byteToHex(ah.spi) << endl;
        cout << "AH Sequence: " << function->byteToHex(ah.sequence) << endl;
        cout << "AH ICV: " << function->byteToHex(ah.icv) << endl;
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

        cout << "ESP: " << endl;
        cout << "ESP SPI: " << function->byteToHex(esp.spi) << endl;
        cout << "ESP Sequence: " << function->byteToHex(esp.sequence) << endl;
    }
}

// Parses and processes the VLAN header.
void Packet::Vlan(string &vlanHeader)
{

    string vlans = function->byteToBin(vlanHeader.substr(0, 2));
    vlan.priority = vlans.substr(0, 3);
    vlan.dei = vlans.substr(3, 1);
    vlan.type = vlanHeader.substr(2, 2);
    start += 4;

    if (print)
    {

        cout << "Vlan: " << endl;
        cout << "Priority: " << vlan.priority << endl;
        cout << "DEI: " << vlan.dei << endl;
        cout << "ID: " << vlan.id << endl;
        cout << "Type: " << function->byteToHex(vlan.type) << endl;
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
        string sec = function->byteToBin(lldpHeader.substr(pos, 2));
        tlv.type = function->binToDec(sec.substr(0, 7));
        tlv.length = function->binToDec(sec.substr(7, 9));
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

        cout << "LLDP Frame:" << endl;
        cout << "Chassis ID: " << lldp.chassisID.value << endl;
        cout << "Port ID: " << lldp.portID.value << endl;
        cout << "TTL: " << lldp.ttl.value << endl;
        cout << "Port Description: " << lldp.portDescription.value << endl;
        cout << "System Name: " << lldp.systemName.value << endl;
        cout << "System Description: " << lldp.systemDescription.value << endl;
        cout << "System Capabilities: " << lldp.systemCapabilities.value << endl;
        cout << "Management Address: " << lldp.managementAddress.value << endl;
        cout << "Organizationally Specific: " << lldp.organizationallySpecific.value << endl;
        cout << "End of LLDPDU" << endl;
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
    string flags = function->byteToBin(dhcpHeader.substr(10, 2));
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
        option.value = dhcpHeader.substr(dhcpStart, function->hexToNum(function->byteToHex(option.length)));
        int dhcpADD = function->hexToNum(function->byteToHex(option.length));
        dhcpStart += dhcpADD;
        start += dhcpADD;
        dhcp.options.push_back(option);
    }

    dhcp.end = dhcpHeader.substr(dhcpStart, 1);
    dhcp.padding = dhcpHeader.substr(dhcpStart + 1);

    if (print)
    {

        cout << "DHCP Frame:" << endl;
        cout << "Message Type: " << function->byteToHex(dhcp.boot) << endl;
        cout << "Hardware Type: " << function->byteToHex(dhcp.hardwareType) << endl;
        cout << "Hardware Address Length: " << function->byteToHex(dhcp.hardwareAddressLength) << endl;
        cout << "Hops: " << function->byteToHex(dhcp.hops) << endl;
        cout << "Transaction ID: " << function->byteToHex(dhcp.transID) << endl;
        cout << "Seconds Elapsed: " << function->byteToHex(dhcp.secondsElapsed) << endl;
        cout << "Broadcast: " << dhcp.bootpFlags.broadcast << endl;
        cout << "Reserved: " << function->byteToHex(dhcp.bootpFlags.reserved) << endl;
        cout << "Client IP: " << function->byteToHex(dhcp.clientIP) << endl;
        cout << "Your Client IP: " << function->byteToHex(dhcp.yourClientIP) << endl;
        cout << "Next Server Address: " << function->byteToHex(dhcp.nextServerIP) << endl;
        cout << "Relay Agent IP: " << function->byteToHex(dhcp.relayAgentIP) << endl;
        cout << "Client MAC Address: " << function->byteToHex(dhcp.clientMacAddress) << endl;
    }
}

// Parses and processes the EIGRP header.
void Packet::Eigrp(string &eigrpHeader)
{
    eigrp.version = eigrpHeader.substr(0, 1);
    eigrp.opcode = eigrpHeader.substr(1, 1);
    eigrp.checksum = eigrpHeader.substr(2, 2);
    string flag = function->byteToBin(eigrpHeader.substr(4, 4));
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
        int eigrpADD = function->hexToNum(function->byteToHex(option.length)) - 4;
        option.value = eigrpHeader.substr(eigrpStart, eigrpADD);
        eigrpStart += eigrpADD;
        start += eigrpADD;
        eigrp.options.push_back(option);
    }

    if (print)
    {
        cout << "EIGRP Frame:" << endl;
        cout << "Version: " << function->byteToHex(eigrp.version) << endl;
        cout << "Opcode: " << function->byteToHex(eigrp.opcode) << endl;
        cout << "Checksum: " << function->byteToHex(eigrp.checksum) << endl;
        cout << "Flags: " << endl;
        cout << "  Init: " << eigrp.flags.init << endl;
        cout << "  Conditional Receive: " << eigrp.flags.conditionalRecieve << endl;
        cout << "  Restart: " << eigrp.flags.restart << endl;
        cout << "  End Of Table: " << eigrp.flags.endOfTable << endl;
        cout << "Sequence Number: " << function->byteToHex(eigrp.sequence) << endl;
        cout << "Acknowledgment Number: " << function->byteToHex(eigrp.ack) << endl;
        cout << "Virtual Router ID: " << function->byteToHex(eigrp.virtualRouterID) << endl;
        cout << "Autonomous System Number: " << function->byteToHex(eigrp.autonomousSystem) << endl;

        cout << "Options:" << endl;
        for (const auto &option : eigrp.options)
        {
            cout << "  Option: " << function->byteToHex(option.option) << endl;
            cout << "  Length: " << function->byteToHex(option.length) << endl;
            cout << "  Value: " << function->byteToHex(option.value) << endl;
        }
    }
}
