#include <Encapsulation.h>
#include <type_traits>
#include <Checksums.h>
#include <Profiler.hpp>

// Encapsulates packet information into a formatted string.
std::optional<ByteString> encapsulate(PacketInfo &packet, ByteString encapsulated)
{
    //Profiler::getInstance().notify("Encapsulation has begun");
    bool success = true;

    // Strings to accumulate header data.
    std::optional<ByteString> encapsulatedHeaders[static_cast<size_t>(HeaderType::Count)] = { std::nullopt };
    
    // Lamda to handle encapsulation and storage
    auto handleEncapsulation = [&](auto& header) -> bool {
        HeaderType enumType = mapHeaderToEnum(header);
        auto encap = header.encapsulate();
        if (encap.has_value())
        {
            encapsulatedHeaders[static_cast<size_t>(enumType)] = std::move(encap.value());
            return true;
        }
        return false;
    };

    // Lamda to get sizes
    auto calculateSize = [&](HeaderType startHeader) -> size_t {
        size_t headerSize = 0;
        for (size_t i = static_cast<size_t>(startHeader) + 1; i < static_cast<size_t>(HeaderType::Count); i++)
        {
            const auto& header = encapsulatedHeaders[i];
            if (header) headerSize += header->size();
        }
        return headerSize;
    };

    // Process layer headers in sequence, direct handling based on type
    auto processLayerHeaders = [&](auto& layerHeaders)
    {
        for (auto& header : layerHeaders)
        {
            std::visit([&](auto& hdr)
            {
                if (!handleEncapsulation(hdr))
                {
                    success = false;
                }
            }, header);
        }
    };

    processLayerHeaders(packet.Layer2);
    processLayerHeaders(packet.Layer2_5);
    processLayerHeaders(packet.Layer3);
    processLayerHeaders(packet.Layer4);
    processLayerHeaders(packet.Layer5);

    if (!success) return std::nullopt;

    // ------------------Final-Calculations-----------------------

    //Profiler::getInstance().notify("Checksum Calculations have started");

    auto processIpv4Headers = [&](ByteString& ipv4)
    {
        if (ipv4.size())
        {
            ByteString ipv4Size = Functions::numToByte(ipv4.size() + calculateSize(HeaderType::IPv4) + encapsulated.size(), 2);
            ipv4.replace(2, 2, ipv4Size);
            Checksum::calculateProtocolChecksum("", ipv4, 10, 2);
        }
    };

    // Process IPv4 headers, TCP/UDP checksums
    if (encapsulatedHeaders[static_cast<size_t>(HeaderType::IPv4)].has_value())
    {
        ByteString& ipv4 = encapsulatedHeaders[static_cast<size_t>(HeaderType::IPv4)].value();
        processIpv4Headers(ipv4);

        // Process TCP, UDP, EIGRP, ect
        if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Tcp)].has_value())
        {
            ByteString& tcp = encapsulatedHeaders[static_cast<size_t>(HeaderType::Tcp)].value();
            if (tcp.size() % 4 != 0) return std::nullopt;

            // Calculate the TCP offset
            uint8_t dataOffset = static_cast<uint8_t>(tcp.size() / 4);
            tcp[12] = (tcp[12] & 0x0F) | (dataOffset << 4);

            // Calculate the pseudo header for TCP
            ByteString tcpSize = Functions::numToByte(tcp.size() + calculateSize(HeaderType::Tcp) + encapsulated.size(), 2);
            ByteString pseudoHeader = ipv4.substr(12, 8) + ByteString(1, 0x00) + ipv4.substr(9, 1) + tcpSize;
            Checksum::calculateProtocolChecksum(encapsulatedHeaders, pseudoHeader, encapsulated, HeaderType::Tcp, 16, 2);
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Udp)].has_value())
        {
            ByteString& udp = encapsulatedHeaders[static_cast<size_t>(HeaderType::Udp)].value();
            ByteString udpSize = Functions::numToByte(udp.size() + calculateSize(HeaderType::Udp) + encapsulated.size(), 2);
            udp.replace(4, 2, udpSize);
            ByteString pseudoHeader = ipv4.substr(12, 8) + ByteString(1, 0x00) + ipv4.substr(9, 1) + udpSize;
            Checksum::calculateProtocolChecksum(encapsulatedHeaders, pseudoHeader, encapsulated, HeaderType::Udp, 6, 2);
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Eigrp)].has_value())
        {
            Checksum::calculateProtocolChecksum("", encapsulatedHeaders[static_cast<size_t>(HeaderType::Eigrp)].value(), 2, 2);
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Icmp)].has_value())
        {
            Checksum::calculateProtocolChecksum(encapsulatedHeaders, "", encapsulated, HeaderType::Icmp, 2, 2);
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Igmp)].has_value())
        {
            Checksum::calculateProtocolChecksum(encapsulatedHeaders, "", encapsulated, HeaderType::Igmp, 2, 2);
        }
    }
    else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::IPv6)].has_value())
    {
        ByteString& ipv6 = encapsulatedHeaders[static_cast<size_t>(HeaderType::IPv6)].value();
        size_t totalSize = calculateSize(HeaderType::IPv6) + encapsulated.size();
        ByteString ipv6Size = Functions::numToByte(totalSize, 2);
        ipv6.replace(4, 2, ipv6Size);

        // Handle TCP, UDP, EIGRP, ect
        if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Tcp)].has_value())
        {
            ByteString& tcp = encapsulatedHeaders[static_cast<size_t>(HeaderType::Tcp)].value();
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Udp)].has_value())
        {
            ByteString& usp = encapsulatedHeaders[static_cast<size_t>(HeaderType::Udp)].value();
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Eigrp)])
        {
            ByteString& eigrp = encapsulatedHeaders[static_cast<size_t>(HeaderType::Eigrp)].value();
        }
        else if (encapsulatedHeaders[static_cast<size_t>(HeaderType::Icmpv6)].has_value())
        {
            ByteString& icmpv6 = encapsulatedHeaders[static_cast<size_t>(HeaderType::Icmpv6)].value();
            uint16_t icmpv6Length = static_cast<uint16_t>(icmpv6.size() + encapsulated.size());

            // Construct the Pseudo-Header for ICMPv6
            ByteString pseudoHeader = ipv6.substr(8, 32); // IPv6 Addresses

            // Upper-Layer Packet Length (4 bytes) in big-endian
            pseudoHeader.push_back(static_cast<ByteString::byte>((icmpv6Length >> 24) & 0xFF));
            pseudoHeader.push_back(static_cast<ByteString::byte>((icmpv6Length >> 16) & 0xFF));
            pseudoHeader.push_back(static_cast<ByteString::byte>((icmpv6Length >> 8) & 0xFF));
            pseudoHeader.push_back(static_cast<ByteString::byte>(icmpv6Length & 0xFF));
            
            // Append Three Zero Bytes
            pseudoHeader.push_back('\x00');
            pseudoHeader.push_back('\x00');
            pseudoHeader.push_back('\x00');

            // Append Next Header = ICMPv6 (58)
            pseudoHeader.push_back('\x3A');

            // Calculate the ICMPv6 checksum using existing function
            Checksum::calculateProtocolChecksum(encapsulatedHeaders, pseudoHeader, encapsulated, HeaderType::Icmpv6, 2, 2, true);

        }
    }
    //Profiler::getInstance().notify("Packet Assembly has started");

    // Assemble the final packet string from all headers and payloads.
    size_t totalSize = encapsulated.size();
    size_t headerCount = static_cast<size_t>(HeaderType::Count);
    for (size_t i = 0; i < headerCount; ++i)
    {
        const auto& encap = encapsulatedHeaders[i];
        if (encap.has_value())
        {
            totalSize += encap->size();
        }
    }
    
    // Initialize the final packet string wither reserved space
    //Profiler::getInstance().notify("Size Calculated");
    ByteString packetString;
    packetString.reserve(totalSize);

    // Concatenate headers in the specified order
    for (size_t i = 0; i < headerCount; ++i)
    {
        const auto& encap = encapsulatedHeaders[i];
        if (encap.has_value())
        {
            packetString.append(encap.value());
        }
    }

    //Profiler::getInstance().notify("Returning Packet");
    // Append encapsulated data to the final packet string.
    return std::move(packetString) + std::move(encapsulated);
}
