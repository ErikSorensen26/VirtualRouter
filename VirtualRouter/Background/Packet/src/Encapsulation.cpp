#include <Encapsulation.h>

// Encapsulates packet information into a formatted string.
std::optional<ByteString> encapsulate(PacketInfo &packet, ByteString encapsulated)
{
    ByteString packetString{};

    ByteString currentVrf;
    std::vector<std::any> data{};

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
            auto encap = eth.encapsulate();
            if (encap.has_value())
            {
                ethernetString = std::move(encap.value());
            }
            else return std::nullopt;
        }
        else if (is_type<PppHeader>(header))
        {

            const PppHeader &ppp = std::any_cast<const PppHeader &>(header);
            auto encap = ppp.encapsulate();
            if (encap.has_value())
            {
                pppString = std::move(encap.value());
            }
            //else return std::nullopt;
        }
    }

    // Process Layer2.5 headers.
    for (auto header : packet.Layer2_5)
    {
        if (is_type<ArpHeader>(header))
        {
            const ArpHeader &arp = std::any_cast<const ArpHeader &>(header);
            auto encap = arp.encapsulate();
            if (encap.has_value())
            {
                arpString = std::move(encap.value());
            }
            else return std::nullopt;
        }
        else if (is_type<MplsHeader>(header))
        {
            const MplsHeader &mpls = std::any_cast<const MplsHeader &>(header);
            auto encap = mpls.encapsulate();
            if (encap.has_value())
            {
                mplsString = std::move(encap.value());
            }
            else return std::nullopt;
        }
        else if (is_type<VlanHeader>(header))
        {
            const VlanHeader &vlan = std::any_cast<const VlanHeader &>(header);
            auto encap = vlan.encapsulate();
            if (encap.has_value())
            {
                vlanString = std::move(encap.value());
            }
            else return std::nullopt;
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
            auto encap = ipv4.encapsulate();
            if (encap.has_value())
            {
                ipv4String = std::move(encap.value());
            }
            else return std::nullopt;
        }
        else if (is_type<IPv6Header>(header))
        {
            const IPv6Header& ipv6 = std::any_cast<const IPv6Header &>(header);
            auto encap = ipv6.encapsulate();
            if (encap.has_value())
            {
                ipv6String = std::move(encap.value());
            }
            //else return std::nullopt;
        }
        else if (is_type<GreHeade>(header))
        {
            const GreHeade &gre = std::any_cast<const GreHeade &>(header);
            auto encap = gre.encapsulate();
            if (encap.has_value())
            {
                greString = std::move(encap.value());
            }
            //else return std::nullopt;
        }
        else if (is_type<AhHeader>(header))
        {
            const AhHeader &ah = std::any_cast<const AhHeader &>(header);
            auto encap = ah.encapsulate();
            if (encap.has_value())
            {
                ahString = std::move(encap.value());
            }
            //else return std::nullopt;
        }
        else if (is_type<EspHeader>(header))
        {
            const EspHeader &esp = std::any_cast<const EspHeader &>(header);
            auto encap = esp.encapsulate();
            if (encap.has_value())
            {
                espString = std::move(encap.value());
            }
            //else return std::nullopt;
        }
        else if (is_type<IcmpHeader>(header))
        {
            const IcmpHeader &icmp = std::any_cast<const IcmpHeader &>(header);
            auto encap = icmp.encapsulate();
            if (encap.has_value())
            {
                icmpString = Checksum::calculateProtocolChecksum(encap.value() + encapsulated, 0, encap.value().size(), 2);
            }
            else return std::nullopt;
        }
        else if (is_type<IcmpV6Header>(header))
        {
            const IcmpV6Header &icmpv6 = std::any_cast<const IcmpV6Header &>(header);
            auto encap = icmpv6.encapsulate();
            if (encap.has_value())
            {
                icmpv6String = Checksum::calculateProtocolChecksum(encap.value() + encapsulated, 0, encap.value().size(), 2);
            }
            else return std::nullopt;
        }
        else if (is_type<IgmpHeader>(header))
        {
            const IgmpHeader &igmp = std::any_cast<const IgmpHeader &>(header);
            auto encap = igmp.encapsulate();
            if (encap.has_value())
            {
                igmpString = Checksum::calculateProtocolChecksum(encap.value(), 0, encap.value().size(), 2);
            }
            else return std::nullopt;
        }
        else if (is_type<EigrpHeader>(header))
        {
            const EigrpHeader &eigrp = std::any_cast<const EigrpHeader &>(header);
            auto encap = eigrp.encapsulate();
            if (encap.has_value())
            {
                eigrpString = std::move(encap.value());
            }
            else return std::nullopt;
        }
    }

    // Process Layer4 headers.
    for (auto header : packet.Layer4)
    {
        if (is_type<TcpHeader>(header))
        {
            const TcpHeader &tcp = std::any_cast<const TcpHeader &>(header);
            auto encap = tcp.encapsulate();
            if (encap.has_value())
            {
                tcpString = std::move(encap.value());
            }
            else return std::nullopt;
        }
        else if (is_type<UdpHeader>(header))
        {
            const UdpHeader &udp = std::any_cast<const UdpHeader &>(header);
            auto encap = udp.encapsulate();
            if (encap.has_value())
            {
                udpString = std::move(encap.value());
            }
            else return std::nullopt;
        }
    }

    // Process Layer5 headers.
    for (auto header : packet.Layer5)
    {
        if (is_type<DhcpHeader>(header))
        {
            const DhcpHeader &dhcp = std::any_cast<const DhcpHeader &>(header);
            auto encap = dhcp.encapsulate();
            if (encap.has_value())
            {
                dhcpString = std::move(encap.value());
            }
            else return std::nullopt;
        }
    }
    ByteString applicationPayload = dhcpString + encapsulated;

    // ------------------Final-Calculations-----------------------

    if (!ipv4String.empty())
    {
        // Calculate the total size of the IPv4 payload including headers and application payload.
        ByteString ipv4Size = Functions::numToByte(static_cast<unsigned int>(ipv4String.size() + udpString.size() + icmpString.size() + tcpString.size() + eigrpString.size() + applicationPayload.size()));
        while (ipv4Size.size() < 2)
        {
            ipv4Size = ByteString(1, 0x00) + ipv4Size;
        }
        ipv4String = ipv4String.replace(2, 2, ipv4Size);

        // Recalculate the IPv4 checksum.
        ipv4String = Checksum::calculateProtocolChecksum(ipv4String, 0, ipv4String.size(), 10);
    }

    if (!ipv4String.empty() && !tcpString.empty())
    {
        if (tcpString.size() % 4 != 0) return std::nullopt;

        // Calculate the size of the TCP header.
        tcpString = tcpString.toString().replace(12, 1, Functions::binToByte(Functions::numToBin(tcpString.size() / 4, 4) + "0000").toString());

        // Calculate the TCP data length by including the application payload.
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << std::hex << Functions::binToNum((Functions::byteToBin(tcpString.substr(12, 1))).substr(0, 4)) * 4 + applicationPayload.size();

        // Construct the pseudo header for TCP checksum calculation.
        ByteString pseudoHeader = ipv4String.substr(12, 8) + ByteString(1, 0x00) + ipv4String.substr(9, 1) + Functions::hexToByte(oss.str());
        ByteString checksumStr = pseudoHeader + tcpString + applicationPayload;

        // Recalculate the TCP checksum.
        tcpString = Checksum::calculateProtocolChecksum(checksumStr, 12, tcpString.size(), 16);
    }

    if (!ipv4String.empty() && !udpString.empty())
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

    if (!ipv4String.empty() && !eigrpString.empty())
    {
        // Recalculate the EIGRP checksum.
        eigrpString = Checksum::calculateProtocolChecksum(eigrpString, 0, eigrpString.size(), 2);
    }

    // Assemble the final packet string from all headers and payloads.
    packetString = ethernetString +
                   pppString + arpString +
                   mplsString + vlanString +
                   lldpString + ipv4String +
                   ipv6String + greString +
                   ahString + espString + 
                   icmpString + icmpv6String +
                   igmpString + tcpString +
                   udpString + eigrpString +
                   dhcpString;

    // Append encapsulated data to the final packet string.
    return packetString + encapsulated;
}
