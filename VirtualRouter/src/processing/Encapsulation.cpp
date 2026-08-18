// Encapsulation.cpp

#include <algorithm>
#include "Encapsulation.h"
#include "PacketBuilder.hpp"
#include "security/Checksums.h"

namespace processing
{

// Encapsulates packet information into a formatted string.
bool encapsulate(PacketBuilder& packet)
{
    auto packetBuffer = packet.getBuffer();
    // Process IPv4 headers, TCP/UDP checksums
    for (int i = 0; i < packet.getHeaderCount(); i++)
    {
        const BuildEntry& header = packet.getHeaders()[i];
        switch (header.type)
        {
            case HeaderType::NONE: break;
            case HeaderType::ETHERNET: break;
            case HeaderType::ARP: break;
            case HeaderType::MPLS: break;
            case HeaderType::IPV4:
            {
                uint16_t ipv4Size = packet.bufferOffset - (header.buffer - packetBuffer);
                utils::write<uint16_t>(header.buffer + 2, ipv4Size);
                security::checksum::calculateChecksum(header.buffer, header.length, 10, 2);
                break;
            }
            case HeaderType::IPV6:
            {
                size_t totalSize = packet.bufferOffset - (header.buffer - packetBuffer);
                uint16_t payloadLen = static_cast<uint16_t>(totalSize - packet::IPv6Header::fixedSize);
                utils::write<uint16_t>(header.buffer + 4, payloadLen);
                break;
            }
            case HeaderType::AH: break;
            case HeaderType::ESP: break;
            case HeaderType::ICMP:
            {
                // ICMPv4 checksum has no pseudo-header, unlike ICMPv6/TCP/UDP.
                security::checksum::calculateChecksum(header.buffer, header.length, 2, 2);
                break;
            }
            case HeaderType::ICMPV6:
            {
                // Construct the Pseudo-Header for ICMPv6
                auto& ip = packet.getHeaders()[i - 1];
                if (ip.type != HeaderType::IPV6) return false;

                uint8_t pseudoHeader[40];
                std::memcpy(pseudoHeader, ip.buffer + 8, 32);
                utils::write<uint32_t>(pseudoHeader + 32, header.length);
                std::memset(pseudoHeader + 36, 0, 3);
                pseudoHeader[39] = IP_ICMPV6;
                security::checksum::calculateChecksum(header.buffer, header.length, 2, 2, pseudoHeader, 40);
                break;
            }
            case HeaderType::TCP:
            {
                if (header.length % 4 != 0) return false;
                uint8_t dataOffset = header.length / 4;
                header.buffer[12] = (header.buffer[12] & 0x0F) | (dataOffset << 4);
                uint16_t size = packet.bufferOffset - (header.buffer - packetBuffer);
                auto ipIt = std::find_if(
                    std::make_reverse_iterator(packet.getHeaders() + i),
                    std::make_reverse_iterator(packet.getHeaders()),
                    [](const BuildEntry& h) { return h.type == HeaderType::IPV4 || h.type == HeaderType::IPV6; }
                );
                if (ipIt == std::make_reverse_iterator(packet.getHeaders())) return false;
                const BuildEntry& ip = *ipIt;

                if (ip.type == HeaderType::IPV4)
                {
                    uint8_t pseudoHeader[12];
                    std::memcpy(pseudoHeader, ip.buffer + 12, 8);
                    pseudoHeader[8] = 0x00;
                    pseudoHeader[9] = IP_TCP;
                    utils::write<uint16_t>(pseudoHeader + 10, size);
                    security::checksum::calculateChecksum(header.buffer, size, 16, 2, pseudoHeader, 12);
                }
                else if (ip.type == HeaderType::IPV6)
                {
                    uint8_t pseudoHeader[40];
                    std::memcpy(pseudoHeader, ip.buffer + 8, 32);
                    utils::write<uint32_t>(pseudoHeader + 32, static_cast<uint32_t>(size));
                    std::memset(pseudoHeader + 36, 0, 3);
                    pseudoHeader[39] = IP_TCP;
                    security::checksum::calculateChecksum(header.buffer, size, 16, 2, pseudoHeader, 40);
                }
                else return false;
                break;
            }
            case HeaderType::UDP:
            {
                uint16_t size = packet.bufferOffset - (header.buffer - packetBuffer);
                utils::write<uint16_t>(header.buffer + 4, size);
                auto ipIt = std::find_if(
                    std::make_reverse_iterator(packet.getHeaders() + i),
                    std::make_reverse_iterator(packet.getHeaders()),
                    [](const BuildEntry& h) { return h.type == HeaderType::IPV4 || h.type == HeaderType::IPV6; }
                );
                if (ipIt == std::make_reverse_iterator(packet.getHeaders())) return false;
                const BuildEntry& ip = *ipIt;

                if (ip.type == HeaderType::IPV4)
                {
                    uint8_t pseudoHeader[12];
                    std::memcpy(pseudoHeader, ip.buffer + 12, 8);
                    pseudoHeader[8] = 0x00;
                    pseudoHeader[9] = IP_UDP;
                    utils::write<uint16_t>(pseudoHeader + 10, size);
                    security::checksum::calculateChecksum(header.buffer, size, 6, 2, pseudoHeader, 12);
                }
                else if (ip.type == HeaderType::IPV6)
                {
                    uint8_t pseudoHeader[40];
                    std::memcpy(pseudoHeader, ip.buffer + 8, 32);
                    utils::write<uint32_t>(pseudoHeader + 32, static_cast<uint32_t>(size));
                    std::memset(pseudoHeader + 36, 0, 3);
                    pseudoHeader[39] = IP_UDP;
                    security::checksum::calculateChecksum(header.buffer, size, 6, 2, pseudoHeader, 40);
                }
                else return false;
                break;
            }
            case HeaderType::EIGRP:
            {
                security::checksum::calculateChecksum(header.buffer, header.length, 2, 2);
                break;
            }
            case HeaderType::DHCP: break;
            case HeaderType::DHCPV6: break;
            case HeaderType::DHCPV6_RELAY: break;
            case HeaderType::ENCAPSULATE: break;
            default: return false;
        }
    }
    return true;
}

} // namespace processing
