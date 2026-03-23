// IPPacket.h

#ifndef IP_PACKET_H
#define IP_PACKET_H

#include <cstdint>
#include <IPAddress.h>
#include <optional>

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

namespace infrastructure::ippacket
{
void reserveIpv4(processing::PacketBuilder& packetInfo);
void reserveIpv6(processing::PacketBuilder& packetInfo);

struct BuildIP
{
    interface::Interface* iface;
    processing::PacketBuilder& packetInfo;
    types::IPAddress destIp;
    std::optional<types::IPAddress> sourceIp = std::nullopt;
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
} // namespace infrastructure::ippacket

#endif // IP_PACKET_H

