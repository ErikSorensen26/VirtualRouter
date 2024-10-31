#include <Encapsulation.h>
#include <typeinfo>

// Encapsulates packet information into a formatted string.
std::string Encapsulate(PacketInfo &packet, string encapsulated)
{
    std::string packetString{};

    // Retrieve the singleton instance of Functions.
    Functions *function = Functions::getInstance();

    string currentVrf;
    vector<any> data{};

    Variable variable;

    // Flags to track the presence of different header types.
    bool hasEthernet{},
        hasPpp{}, hasArp{},
        hasMpls{}, hasVlan{},
        hasLldp{}, hasIpv4{},
        hasGre{}, hasAh{},
        hasEsp{}, hasIcmp{},
        hasIgmp{}, hasTcp{},
        hasUdp{}, hasDhcp{},
        hasEigrp{};

    // Strings to accumulate header data.
    string ethernetString{},
        pppString{}, arpString{},
        mplsString{}, vlanString{},
        lldpString{}, ipv4String{},
        greString{}, ahString{},
        espString{}, icmpString{},
        igmpString{}, tcpString{},
        udpString{}, dhcpString{},
        eigrpString{};

    // Process Layer2 headers.
    for (auto header : packet.Layer2)
    {
        if (is_type<ethernetHeader>(header))
        {
            const ethernetHeader &eth = std::any_cast<const ethernetHeader &>(header);
            ethernetString += eth.destinationMac;
            ethernetString += eth.sourceMac;
            ethernetString += eth.type;
            hasEthernet = true;
        }
        else if (is_type<pppHeader>(header))
        {
            const pppHeader &ppp = std::any_cast<const pppHeader &>(header);
            pppString += ppp.address;
            pppString += ppp.control;
            pppString += ppp.protocol;
            hasPpp = true;
        }
    }

    // Process Layer2.5 headers.
    for (auto header : packet.Layer2_5)
    {
        if (is_type<arpHeader>(header))
        {
            const arpHeader &arp = std::any_cast<const arpHeader &>(header);
            arpString += arp.hardwareType;
            arpString += arp.protocolType;
            arpString += arp.hardwareSize;
            arpString += arp.protocolSize;
            arpString += arp.opcode;
            arpString += arp.senderHardwareAddress;
            arpString += arp.senderIpAddress;
            arpString += arp.targetHardwareAddress;
            arpString += arp.targetIpAddress;
            hasArp = true;
        }
        else if (is_type<mplsHeader>(header))
        {
            const mplsHeader &mpls = std::any_cast<const mplsHeader &>(header);
            mplsString += mpls.label;
            // mplsString += function->binToHex(mpls.expBit + mpls.bottomLabelStack);
            mplsString += mpls.TTL;
            mplsString += function->hexToByte(mplsString);
            hasMpls = true;
        }
        else if (is_type<vlanHeader>(header))
        {
            const vlanHeader &vlan = std::any_cast<const vlanHeader &>(header);
            vlanString += vlan.priority;
            vlanString += vlan.dei;
            vlanString += vlan.id;
            vlanString += vlan.type;
            hasVlan = true;
        }
        else if (is_type<lldpHeader>(header))
        {
            const lldpHeader &lldp = std::any_cast<const lldpHeader &>(header);
            // No processing for LLDP in this implementation.
        }
    }

    // Process Layer3 headers.
    for (auto header : packet.Layer3)
    {
        if (is_type<ipv4Header>(header))
        {
            const ipv4Header &ipv4 = std::any_cast<const ipv4Header &>(header);
            ipv4String += function->hexToByte(ipv4.version + ipv4.headerLength);
            ipv4String += ipv4.serviceField;
            ipv4String += ipv4.totalLength;
            ipv4String += ipv4.identification;
            ipv4String += function->binToByte(ipv4.fragmentFlag.reserved + ipv4.fragmentFlag.fragment + ipv4.fragmentFlag.moreFragment + ipv4.fragmentFlag.fragmentOffset);
            ipv4String += ipv4.TTL;
            ipv4String += ipv4.protocol;
            ipv4String += function->hexToByte("0000");
            ipv4String += ipv4.sourceAddress;
            ipv4String += ipv4.destinationAddress;
            ipv4String += function->binToByte(ipv4.options.type.copy) + ipv4.options.type.classControl + ipv4.options.type.routerAlert;
            ipv4String += ipv4.options.length;
            ipv4String += ipv4.options.routerAlert;
            hasIpv4 = true;
        }
        else if (is_type<greHeade>(header))
        {
            const greHeade &gre = std::any_cast<const greHeade &>(header);
            greString += function->binToByte(gre.flags.checksum + gre.flags.routing + gre.flags.key + gre.flags.seqNum + gre.flags.strictSourceRoute + gre.flags.recursion + gre.flags.acknowledgment + gre.flags.recursion + gre.flags.version);
            greString += gre.protocol;
            greString += gre.length;
            greString += gre.callID;
            greString += gre.seqNum;
            hasGre = true;
        }
        else if (is_type<ahHeader>(header))
        {
            const ahHeader &ah = std::any_cast<const ahHeader &>(header);
            ahString += ah.next;
            ahString += ah.length;
            ahString += ah.reserved;
            ahString += ah.spi;
            ahString += ah.sequence;
            ahString += ah.icv;
            hasAh = true;
        }
        else if (is_type<espHeader>(header))
        {
            const espHeader &esp = std::any_cast<const espHeader &>(header);
            espString += esp.spi;
            espString += esp.sequence;
            hasEsp = true;
        }
        else if (is_type<icmpHeader>(header))
        {
            const icmpHeader &icmp = std::any_cast<const icmpHeader &>(header);
            icmpString += icmp.type;
            icmpString += icmp.code;
            icmpString += function->hexToByte("0000");
            icmpString += icmp.identifier;
            icmpString += icmp.sequenceNumber;
            icmpString = Checksum::CalculateProtocolChecksum(icmpString + encapsulated, icmpString.size(), 8, 2);
            hasIcmp = true;
        }
        else if (is_type<igmpHeader>(header))
        {
            const igmpHeader &igmp = std::any_cast<const igmpHeader &>(header);
            igmpString += igmp.type;
            igmpString += igmp.maxRestTime;
            igmpString += function->hexToByte("0000");
            igmpString += igmp.multicastAddress;
            igmpString += function->binToByte(igmp.v3.supress + igmp.v3.qrv + igmp.v3.qqic + igmp.v3.numSrc);
            igmpString = Checksum::CalculateProtocolChecksum(igmpString, 0, igmpString.size(), 2);
            hasIgmp = true;
        }
        else if (is_type<eigrpHeader>(header))
        {
            const eigrpHeader &eigrp = std::any_cast<const eigrpHeader &>(header);
            eigrpString += eigrp.version;
            eigrpString += eigrp.opcode;
            eigrpString += function->hexToByte("0000");
            eigrpString += function->binToByte("0000000000000000000000000000" + eigrp.flags.init + eigrp.flags.conditionalRecieve + eigrp.flags.restart + eigrp.flags.endOfTable);
            eigrpString += eigrp.sequence;
            eigrpString += eigrp.ack;
            eigrpString += eigrp.virtualRouterID;
            eigrpString += eigrp.autonomousSystem;
            for (auto opt : eigrp.options)
            {
                eigrpString += opt.option;
                eigrpString += opt.length;
                eigrpString += opt.value;
            }
            hasEigrp = true;
        }
    }

    // Process Layer4 headers.
    for (auto header : packet.Layer4)
    {
        if (is_type<tcpHeader>(header))
        {
            const tcpHeader &tcp = std::any_cast<const tcpHeader &>(header);
            tcpString += tcp.sourcePort;
            tcpString += tcp.destinationPort;
            tcpString += tcp.sequenceNumber;
            tcpString += tcp.ackNumber;
            tcpString += tcp.headerLength;
            tcpString += function->binToByte(tcp.flags.congestionWindowReduced + tcp.flags.ecnEcho + tcp.flags.urgent + tcp.flags.acknowledgement + tcp.flags.push + tcp.flags.reset + tcp.flags.syn + tcp.flags.fin);
            tcpString += tcp.windowSize;
            tcpString += function->hexToByte("0000");
            tcpString += tcp.urgentPointer;
            // Add TCP options.
            for (auto opt : tcp.options)
            {
                tcpString += opt.type;
                tcpString += opt.length;
                tcpString += opt.value;
            }
            hasTcp = true;
        }
        else if (is_type<udpHeader>(header))
        {
            const udpHeader &udp = std::any_cast<const udpHeader &>(header);
            udpString += udp.sourcePort;
            udpString += udp.destinationPort;
            udpString += udp.length;
            udpString += function->hexToByte("0000");
            hasUdp = true;
        }
    }

    // Process Layer5 headers.
    for (auto header : packet.Layer5)
    {
        if (is_type<dhcpHeader>(header))
        {
            const dhcpHeader &dhcp = std::any_cast<const dhcpHeader &>(header);
            dhcpString += dhcp.boot;
            dhcpString += dhcp.hardwareType;
            dhcpString += dhcp.hardwareAddressLength;
            dhcpString += dhcp.hops;
            dhcpString += dhcp.transID;
            dhcpString += dhcp.secondsElapsed;
            dhcpString += function->binToByte(dhcp.bootpFlags.broadcast + dhcp.bootpFlags.reserved);
            dhcpString += dhcp.clientIP;
            dhcpString += dhcp.yourClientIP;
            dhcpString += dhcp.nextServerIP;
            dhcpString += dhcp.relayAgentIP;
            dhcpString += dhcp.clientMacAddress;
            dhcpString += dhcp.clientHardwareAddressPadding;
            dhcpString += dhcp.serverHostName;
            dhcpString += dhcp.bootFile;
            dhcpString += dhcp.magicCookie;
            for (dhcpHeader::Option opt : dhcp.options)
            {
                dhcpString += opt.option;
                dhcpString += opt.length;
                dhcpString += opt.value;
            }
            dhcpString += dhcp.end;
            dhcpString += dhcp.padding;
            hasDhcp = true;
        }
    }
    string applicationPayload = dhcpString + encapsulated;

    // ------------------Final-Calculations-----------------------

    if (hasIpv4)
    {
        // Calculate the total size of the IPv4 payload including headers and application payload.
        string ipv4Size = function->intToHex(ipv4String.size() + udpString.size() + tcpString.size() + eigrpString.size() + applicationPayload.size());
        while (ipv4Size.size() < 4)
        {
            ipv4Size = "0" + ipv4Size;
        }
        ipv4String.replace(2, 2, function->hexToByte(ipv4Size));

        // Recalculate the IPv4 checksum.
        ipv4String = Checksum::CalculateProtocolChecksum(ipv4String, 0, ipv4String.size(), 10);
    }

    if (hasIpv4 && hasTcp)
    {
        // Calculate the size of the TCP header.
        string tcpSize = function->intToHex(tcpString.size());
        while (tcpSize.size() < 2)
        {
            tcpSize = "0" + tcpSize;
        }
        tcpString.replace(12, 1, function->hexToByte(tcpSize));

        // Calculate the TCP data length by including the application payload.
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << std::hex << function->binToDec((function->byteToBin(tcpString.substr(12, 1))).substr(0, 4)) * 4 + applicationPayload.size();

        // Construct the pseudo header for TCP checksum calculation.
        string pseudoHeader = ipv4String.substr(12, 8) + function->hexToByte("00") + ipv4String.substr(9, 1) + function->hexToByte(oss.str());
        string checksumStr = pseudoHeader + tcpString + applicationPayload;

        // Recalculate the TCP checksum.
        tcpString = Checksum::CalculateProtocolChecksum(checksumStr, 12, tcpString.size(), 16);
    }

    if (hasIpv4 && hasUdp)
    {
        // Calculate the size of the UDP header including the application payload.
        string udpSize = function->intToHex(udpString.size() + applicationPayload.size());
        while (udpSize.size() < 4)
        {
            udpSize = "0" + udpSize;
        }
        udpString.replace(4, 2, function->hexToByte(udpSize));
        string oss = udpString.substr(4, 2);

        // Construct the pseudo header for UDP checksum calculation.
        string pseudoHeader = ipv4String.substr(12, 8) + function->hexToByte("00") + ipv4String.substr(9, 1) + (oss);
        string checksumStr = pseudoHeader + udpString + applicationPayload;

        // Recalculate the UDP checksum.
        udpString = Checksum::CalculateProtocolChecksum(checksumStr, 12, 8, 6);
    }

    if (hasIpv4 && hasEigrp)
    {
        // Recalculate the EIGRP checksum.
        eigrpString = Checksum::CalculateProtocolChecksum(eigrpString, 0, eigrpString.size(), 2);
    }

    // Assemble the final packet string from all headers and payloads.
    packetString = ethernetString +
                   pppString + arpString +
                   mplsString + vlanString +
                   lldpString + ipv4String +
                   greString + ahString +
                   espString + icmpString +
                   igmpString + tcpString +
                   udpString + eigrpString +
                   dhcpString;

    // Append encapsulated data to the final packet string.
    return packetString + encapsulated;
}