/**
 * @file IPPacket.h
 * @brief Helpers for constructing IPv4 and IPv6 packets with proper header setup.
 */

#ifndef IP_PACKET_H
#define IP_PACKET_H

#include <cstdint>
#include <IPAddress.h>
#include <optional>

namespace interface { class Interface; }
namespace processing { class PacketBuilder; }

namespace infrastructure::ippacket
{

/**
 * @brief Reserve header space for an IPv4 packet in the builder.
 *
 * Prepends space for the IPv4 header (20 bytes minimum) into the packet builder.
 * Must be called before buildIpv4() to establish the write position.
 *
 * @param packetInfo PacketBuilder to reserve space in.
 */
void reserveIpv4(processing::PacketBuilder& packetInfo);

/**
 * @brief Reserve header space for an IPv6 packet in the builder.
 *
 * Prepends space for the IPv6 header (40 bytes) into the packet builder.
 * Must be called before buildIpv6() to establish the write position.
 *
 * @param packetInfo PacketBuilder to reserve space in.
 */
void reserveIpv6(processing::PacketBuilder& packetInfo);

/**
 * @brief Configuration for building an IP packet.
 * @ingroup INFRASTRUCTURE
 *
 * Holds all parameters needed to construct an IPv4 or IPv6 header:
 * addressing (source, destination, optional MAC), DSCP marking, TTL/hop limit,
 * and fragmentation controls. Passed to buildIpv4() or buildIpv6().
 *
 * ## Member Semantics
 * - @p sourceIp: If not provided, the best local address on @p iface is selected.
 * - @p destMac: If not provided, NDP/ARP resolution is performed.
 * - @p dontFragment, @p moreFragment, @p fragmentOffset: Configure IPv4/IPv6 fragmentation.
 * - @p reserved: Indicates header space has been pre-allocated; set by reserve*() calls.
 */
struct BuildIP
{
    interface::Interface* iface;                              ///< Interface to transmit from.
    processing::PacketBuilder& packetInfo;                   ///< Packet builder to write header into.
    types::IPAddress destIp;                                 ///< Destination IP address (required).
    std::optional<types::IPAddress> sourceIp = std::nullopt; ///< Source IP (optional; auto-selected if missing).
    std::optional<uint64_t> destMac = std::nullopt;          ///< Destination MAC (optional; resolved via NDP/ARP if missing).
    uint8_t DSCP = 0;                                        ///< DSCP marking (0-63).
    uint8_t hopLimit = 255;                                  ///< TTL (IPv4) or Hop Limit (IPv6).
    uint8_t protocolType;                                    ///< IP protocol number (e.g., IPPROTO_TCP, IPPROTO_UDP).
    bool reserved = false;                                   ///< True if header space was pre-allocated.
    bool dontFragment = true;                                ///< IPv4 DF flag (true = fragment forbidden).
    bool moreFragment = false;                               ///< IPv4 MF flag (true = more fragments pending).
    uint16_t fragmentOffset = 0;                             ///< IPv4 fragment offset in 8-byte units.
};

/**
 * @brief Constructs an IPv4 header into reserved space.
 *
 * Writes a complete IPv4 header to the position established by reserveIpv4().
 * If sourceIp is not set, selects the best local address on the interface.
 * If destMac is not set, performs NDP/ARP resolution.
 *
 * @param ipv4Build Configuration for the IPv4 header.
 *
 * @warning sourceIp and destMac resolution may fail silently; callers should
 * check that the packet is valid before transmission.
 *
 * @see reserveIpv4, buildIpv6
 */
void buildIpv4(BuildIP& ipv4Build);

/**
 * @brief Constructs an IPv6 header into reserved space.
 *
 * Writes a complete IPv6 header to the position established by reserveIpv6().
 * If sourceIp is not set, selects the best local address on the interface.
 * If destMac is not set, performs NDP/ARP resolution.
 *
 * @param ipv6Build Configuration for the IPv6 header.
 * @param v6FlowLabel Optional IPv6 flow label (0-0xFFFFF). Defaults to 0.
 *
 * @warning sourceIp and destMac resolution may fail silently; callers should
 * check that the packet is valid before transmission.
 *
 * @see reserveIpv6, buildIpv4
 */
void buildIpv6(
    BuildIP& ipv6Build,
    uint32_t v6FlowLabel = 0
);

} // namespace infrastructure::ippacket

#endif // IP_PACKET_H

