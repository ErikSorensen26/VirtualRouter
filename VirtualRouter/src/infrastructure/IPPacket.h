// IPPacket.h

#ifndef IP_PACKET_H
#define IP_PACKET_H

#include <cstdint>
#include <IPAddress.h>
#include <optional>

class Interface;
class PacketBuilder;

namespace Protocol::IPPacket
{
void reserveIpv4(PacketBuilder& packetInfo);
void reserveIpv6(PacketBuilder& packetInfo);

struct BuildIP
{
    Interface* iface;
    PacketBuilder& packetInfo;
    IPAddress destIp;
    std::optional<IPAddress> sourceIp = std::nullopt;
    std::optional<uint64_t> destMac = std::nullopt;
    uint8_t DSCP = 0;
    uint8_t hopLimit = 255;
    uint8_t protocolType;
    bool reserved = false;
    bool dontFragment = true;
    bool moreFragment = false;
    uint16_t fragmentOffset = 0;
};

void buildIpv4(
    BuildIP& ipv4Build
);

void buildIpv6(
    BuildIP& ipv6Build,
    uint32_t v6FlowLabel = 0
);
} // Namespace Protocol::IPPacket

#endif // IP_PACKET_H
