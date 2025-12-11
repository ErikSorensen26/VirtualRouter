// Udp.h

#ifndef UDP_H
#define UDP_H

#include <cstdint>

enum class AddressFamily : uint8_t;
namespace IPPacket
{
    struct BuildIP;
}
class PacketBuilder;

namespace Protocol::UDP
{
void reserveUDP(PacketBuilder& packetInfo, AddressFamily af);

// Sets the UPD header in PacketInfo
void buildUdp(
    AddressFamily af,
    IPPacket::BuildIP& ipBuild,
    uint16_t sourcePort,
    uint16_t destinationPort,
    uint16_t fragmentOffset = 0
);
}

#endif // UDP_H
