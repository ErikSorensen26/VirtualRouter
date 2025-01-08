#include <Encapsulation.h>

// Encapsulates packet information into a formatted string.
ByteString encapsulate(PacketInfo &packet, ByteString encapsulated)
{
    ByteString packetString{};

    ByteString currentVrf;
    std::vector<std::any> data{};

    // Flags to track the presence of different header types.
    bool hasEthernet{},
        hasPpp{}, hasArp{},
        hasMpls{}, hasVlan{},
        hasLldp{}, hasIpv4{},
        hasGre{}, hasAh{},
        hasEsp{}, hasIcmp{},
        hasIgmp{}, hasTcp{},
        hasUdp{}, hasDhcp{},
        hasEigrp{}, hasIpv6{},
        hasIcmpv6{};

    // Strings to accumulate header data.
    ByteString ethernetString{},
        pppString{}, arpString{},
        mplsString{}, vlanString{},
        lldpString{}, ipv4String{},
        greString{}, ahString{},
        espString{}, icmpString{},
        igmpString{}, tcpString{},
        udpString{}, dhcpString{},
        eigrpString{}, ipv6String{},
        icmpv6String{};

    // Process Layer2 headers.
    for (auto header : packet.Layer2)
    {
        if (is_type<EthernetHeader>(header))
        {
            const EthernetHeader &eth = std::any_cast<const EthernetHeader &>(header);
            ethernetString += eth.destinationMac.size() == 6 ? eth.destinationMac : ByteString(6, '\x00');
            // ethernetString += eth.destinationMac;
            ethernetString += eth.sourceMac;
            ethernetString += eth.type;
            hasEthernet = true;
        }
        else if (is_type<PppHeader>(header))
        {
            const PppHeader &ppp = std::any_cast<const PppHeader &>(header);
            pppString += ppp.address;
            pppString += ppp.control;
            pppString += ppp.protocol;
            hasPpp = true;
        }
    }

    // Process Layer2.5 headers.
    for (auto header : packet.Layer2_5)
    {
        if (is_type<ArpHeader>(header))
        {
            const ArpHeader &arp = std::any_cast<const ArpHeader &>(header);
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
        else if (is_type<MplsHeader>(header))
        {
            const MplsHeader &mpls = std::any_cast<const MplsHeader &>(header);
            mplsString += mpls.label;
            // mplsString += Functions::binToHex(mpls.expBit + mpls.bottomLabelStack);
            mplsString += mpls.TTL;
            mplsString += mplsString.toHex();
            hasMpls = true;
        }
        else if (is_type<VlanHeader>(header))
        {
            const VlanHeader &vlan = std::any_cast<const VlanHeader &>(header);
            vlanString += vlan.priority;
            vlanString += vlan.dei;
            vlanString += vlan.id;
            vlanString += vlan.type;
            hasVlan = true;
        }
        else if (is_type<LldpHeader>(header))
        {
            const LldpHeader &lldp = std::any_cast<const LldpHeader &>(header);
            // No processing for LLDP in this implementation.
        }
    }

    // Process Layer3 headers.
    for (auto header : packet.Layer3)
    {
        if (is_type<IPv4Header>(header))
        {
            const IPv4Header &ipv4 = std::any_cast<const IPv4Header &>(header);
            ipv4String += Functions::hexToByte(ipv4.version + ipv4.headerLength);
            ipv4String += ipv4.serviceField;
            ipv4String += ipv4.totalLength;
            ipv4String += ipv4.identification;
            ipv4String += Functions::binToByte(ipv4.fragmentFlag.reserved + ipv4.fragmentFlag.fragment + ipv4.fragmentFlag.moreFragment + ipv4.fragmentFlag.fragmentOffset);
            ipv4String += ipv4.TTL;
            ipv4String += ipv4.protocol;
            ipv4String += ByteString(2, 0x00);
            ipv4String += ipv4.sourceAddress;
            ipv4String += ipv4.destinationAddress;
            ipv4String += Functions::binToByte(ipv4.options.type.copy) + ipv4.options.type.classControl + ipv4.options.type.routerAlert;
            ipv4String += ipv4.options.length;
            ipv4String += ipv4.options.routerAlert;
            hasIpv4 = true;
        }
        else if (is_type<IPv6Header>(header))
        {
            const IPv6Header& ipv6 = std::any_cast<const IPv6Header &>(header);
            ipv6String += Functions::hexToByte(ipv6.version + ipv6.trafficClass + ipv6.flowLabel);
            ipv6String += ipv6.payloadLength;
            ipv6String += ipv6.protocol;
            ipv6String += ipv6.hopLimit;
            ipv6String += ipv6.sourceAddress;
            ipv6String += ipv6.destinationAddress;
            hasIpv6 = true;
        }
        else if (is_type<GreHeade>(header))
        {
            const GreHeade &gre = std::any_cast<const GreHeade &>(header);
            greString += Functions::binToByte(gre.flags.checksum + gre.flags.routing + gre.flags.key + gre.flags.seqNum + gre.flags.strictSourceRoute + gre.flags.recursion + gre.flags.acknowledgment + gre.flags.recursion + gre.flags.version);
            greString += gre.protocol;
            greString += gre.length;
            greString += gre.callID;
            greString += gre.seqNum;
            hasGre = true;
        }
        else if (is_type<AhHeader>(header))
        {
            const AhHeader &ah = std::any_cast<const AhHeader &>(header);
            ahString += ah.next;
            ahString += ah.length;
            ahString += ah.reserved;
            ahString += ah.spi;
            ahString += ah.sequence;
            ahString += ah.icv;
            hasAh = true;
        }
        else if (is_type<EspHeader>(header))
        {
            const EspHeader &esp = std::any_cast<const EspHeader &>(header);
            espString += esp.spi;
            espString += esp.sequence;
            hasEsp = true;
        }
        else if (is_type<IcmpHeader>(header))
        {
            const IcmpHeader &icmp = std::any_cast<const IcmpHeader &>(header);
            icmpString += icmp.type;
            icmpString += icmp.code;
            icmpString += ByteString(2, 0x00);
            icmpString += icmp.identifier;
            icmpString += icmp.sequenceNumber;
            icmpString = Checksum::calculateProtocolChecksum(icmpString + encapsulated, icmpString.size(), 8, 2);
            hasIcmp = true;
        }
        else if (is_type<IcmpV6Header>(header))
        {
            const IcmpV6Header &icmpv6 = std::any_cast<const IcmpV6Header &>(header);
            icmpv6String += icmpv6.type;
            icmpv6String += icmpv6.code;
            icmpv6String += ByteString(2, 0x00);
            icmpv6String += icmpv6.reserved;
            for (const auto& opt : icmpv6.options)
            {
                icmpv6String += opt.option;
                icmpv6String += opt.length;
                icmpv6String += opt.value;
            }
            hasIcmpv6 = true;
        }
        else if (is_type<IgmpHeader>(header))
        {
            const IgmpHeader &igmp = std::any_cast<const IgmpHeader &>(header);
            igmpString += igmp.type;
            igmpString += igmp.maxRestTime;
            igmpString += ByteString(2, 0x00);
            igmpString += igmp.multicastAddress;
            igmpString += Functions::binToByte(igmp.v3.supress + igmp.v3.qrv + igmp.v3.qqic + igmp.v3.numSrc);
            igmpString = Checksum::calculateProtocolChecksum(igmpString, 0, igmpString.size(), 2);
            hasIgmp = true;
        }
        else if (is_type<EigrpHeader>(header))
        {
            const EigrpHeader &eigrp = std::any_cast<const EigrpHeader &>(header);
            eigrpString += eigrp.version;
            eigrpString += eigrp.opcode;
            eigrpString += ByteString(2, 0x00);
            eigrpString += Functions::binToByte(ByteString("0000000000000000000000000000") + eigrp.flags.endOfTable + eigrp.flags.restart + eigrp.flags.conditionalRecieve + eigrp.flags.init);
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
        if (is_type<TcpHeader>(header))
        {
            const TcpHeader &tcp = std::any_cast<const TcpHeader &>(header);
            tcpString += tcp.sourcePort;
            tcpString += tcp.destinationPort;
            tcpString += tcp.sequenceNumber;
            tcpString += tcp.ackNumber;
            tcpString += tcp.headerLength;
            tcpString += Functions::binToByte(tcp.flags.congestionWindowReduced + tcp.flags.ecnEcho + tcp.flags.urgent + tcp.flags.acknowledgement + tcp.flags.push + tcp.flags.reset + tcp.flags.syn + tcp.flags.fin);
            tcpString += tcp.windowSize;
            tcpString += ByteString(2, 0x00);
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
        else if (is_type<UdpHeader>(header))
        {
            const UdpHeader &udp = std::any_cast<const UdpHeader &>(header);
            udpString += udp.sourcePort;
            udpString += udp.destinationPort;
            udpString += udp.length;
            udpString += ByteString(2, 0x00);
            hasUdp = true;
        }
    }

    // Process Layer5 headers.
    for (auto header : packet.Layer5)
    {
        if (is_type<DhcpHeader>(header))
        {
            const DhcpHeader &dhcp = std::any_cast<const DhcpHeader &>(header);
            dhcpString += dhcp.boot;
            dhcpString += dhcp.hardwareType;
            dhcpString += dhcp.hardwareAddressLength;
            dhcpString += dhcp.hops;
            dhcpString += dhcp.transID;
            dhcpString += dhcp.secondsElapsed;
            dhcpString += Functions::binToByte(dhcp.bootpFlags.broadcast + dhcp.bootpFlags.reserved);
            dhcpString += dhcp.clientIP;
            dhcpString += dhcp.yourClientIP;
            dhcpString += dhcp.nextServerIP;
            dhcpString += dhcp.relayAgentIP;
            dhcpString += dhcp.clientMacAddress;
            dhcpString += dhcp.clientHardwareAddressPadding;
            dhcpString += dhcp.serverHostName;
            dhcpString += dhcp.bootFile;
            dhcpString += dhcp.magicCookie;
            for (DhcpHeader::Option opt : dhcp.options)
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
    ByteString applicationPayload = dhcpString + encapsulated;

    // ------------------Final-Calculations-----------------------

    if (hasIpv4)
    {
        // Calculate the total size of the IPv4 payload including headers and application payload.
        ByteString ipv4Size = Functions::numToByte(static_cast<unsigned int>(ipv4String.size() + udpString.size() + tcpString.size() + eigrpString.size() + applicationPayload.size()));
        while (ipv4Size.size() < 2)
        {
            ipv4Size = ByteString(1, 0x00) + ipv4Size;
        }
        ipv4String = ipv4String.replace(2, 2, ipv4Size);

        // Recalculate the IPv4 checksum.
        ipv4String = Checksum::calculateProtocolChecksum(ipv4String, 0, ipv4String.size(), 10);
    }

    if (hasIpv4 && hasTcp)
    {
        // Calculate the size of the TCP header.
        tcpString = tcpString.toString().replace(12, 1, std::to_string(static_cast<char>(tcpString.size())));

        // Calculate the TCP data length by including the application payload.
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << std::hex << Functions::binToNum((Functions::byteToBin(tcpString.substr(12, 1))).substr(0, 4)) * 4 + applicationPayload.size();

        // Construct the pseudo header for TCP checksum calculation.
        ByteString pseudoHeader = ipv4String.substr(12, 8) + ByteString(1, 0x00) + ipv4String.substr(9, 1) + Functions::hexToByte(oss.str());
        ByteString checksumStr = pseudoHeader + tcpString + applicationPayload;

        // Recalculate the TCP checksum.
        tcpString = Checksum::calculateProtocolChecksum(checksumStr, 12, tcpString.size(), 16);
    }

    if (hasIpv4 && hasUdp)
    {
        // Calculate the size of the UDP header including the application payload.
        ByteString udpSize = Functions::numToByte(static_cast<unsigned int>(udpString.size() + applicationPayload.size()), 2);
        udpString = udpString.replace(4, 2, udpSize);

        // Construct the pseudo header for UDP checksum calculation.
        ByteString pseudoHeader = ipv4String.substr(12, 8) + ByteString(1, 0x00) + ipv4String.substr(9, 1) + udpSize;
        ByteString checksumStr = pseudoHeader + udpString + applicationPayload;

        // Recalculate the UDP checksum.
        udpString = Checksum::calculateProtocolChecksum(checksumStr, 12, 8, 6);
    }

    if (hasIpv4 && hasEigrp)
    {
        // Recalculate the EIGRP checksum.
        eigrpString = Checksum::calculateProtocolChecksum(eigrpString, 0, eigrpString.size(), 2);
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
