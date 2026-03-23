// Udp.h

#ifndef UDP_H
#define UDP_H

#include <cstdint>
#include <AddressFamily.hpp>

namespace processing { class PacketBuilder; }
namespace infrastructure { namespace ippacket { struct BuildIP; } }

namespace transport::udp
{
void reserveUDP(processing::PacketBuilder& packetInfo, types::AddressFamily af);

// Sets the UPD header in PacketInfo
void buildUdp(
    types::AddressFamily af,
    infrastructure::ippacket::BuildIP& ipBuild,
    uint16_t sourcePort,
    uint16_t destinationPort,
    uint16_t fragmentOffset = 0
);
} // namespace transport::udp

#endif // UDP_H

