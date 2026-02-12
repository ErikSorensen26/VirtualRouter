// Udp.cpp

#include "Udp.h"
#include <AddressFamily.hpp>
#include "infrastructure/IPPacket.h"
#include "processing/PacketBuilder.hpp"

namespace Protocol::UDP
{
void reserveUDP(PacketBuilder& packetInfo, AddressFamily af)
{
    af == AddressFamily::IPv4 ? IPPacket::reserveIpv4(packetInfo)
        : IPPacket::reserveIpv6(packetInfo);

    packetInfo.reserveHeader(HeaderType::UDP, sizeof(UdpHeader));
}

// Sets the UPD header in PacketInfo
void buildUdp(
    AddressFamily af,
    IPPacket::BuildIP& ipBuild,
    uint16_t sourcePort,
    uint16_t destinationPort,
    uint16_t fragmentOffset
)
{
    BuildEntry* nextHeader = ipBuild.packetInfo.nextBuildHeader();
    if (!nextHeader || nextHeader->type != HeaderType::IPV6)
        return; // Drop Packet

    UdpHeader udp;
    udp.setBuffer(nextHeader->buffer);
    udp.setChecksum(0);
    udp.setSourcePort(sourcePort);
    udp.setDestinationPort(destinationPort);

    if (af == AddressFamily::IPv4)
        IPPacket::buildIpv4(ipBuild);
    else
        IPPacket::buildIpv6(ipBuild);
}
} // Namespace Protocol::UDP
