// Udp.cpp

#include "Udp.h"
#include <AddressFamily.hpp>
#include "infrastructure/IPPacket.h"
#include "processing/PacketBuilder.hpp"

namespace transport::udp
{
void reserveUDP(processing::PacketBuilder& packetInfo, types::AddressFamily af)
{
    af == types::AddressFamily::IPv4 ? infrastructure::ippacket::reserveIpv4(packetInfo)
        : infrastructure::ippacket::reserveIpv6(packetInfo);

    packetInfo.reserveHeader(packet::HeaderType::UDP, sizeof(packet::UdpHeader));
}

// Sets the UPD header in PacketInfo
void buildUdp(
    types::AddressFamily af,
    infrastructure::ippacket::BuildIP& ipBuild,
    uint16_t sourcePort,
    uint16_t destinationPort,
    uint16_t fragmentOffset
)
{
    processing::BuildEntry* nextHeader = ipBuild.packetInfo.nextBuildHeader();
    if (!nextHeader || nextHeader->type != packet::HeaderType::IPV6)
        return; // Drop Packet

    packet::UdpHeader udp;
    udp.setBuffer(nextHeader->buffer);
    udp.setChecksum(0);
    udp.setSourcePort(sourcePort);
    udp.setDestinationPort(destinationPort);

    if (af == types::AddressFamily::IPv4)
        infrastructure::ippacket::buildIpv4(ipBuild);
    else
        infrastructure::ippacket::buildIpv6(ipBuild);
}
} // namespace transport::udp
