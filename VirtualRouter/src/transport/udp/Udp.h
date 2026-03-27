/**
 * @file Udp.h
 * @brief Stateless helpers for reserving and building UDP headers in a PacketBuilder pipeline.
 */

/**
 * @defgroup UDP UDP
 * @ingroup TRANSPORT
 * @brief UDP header building helpers.
 */

#ifndef UDP_H
#define UDP_H

#include <cstdint>
#include <AddressFamily.hpp>

namespace processing { class PacketBuilder; }
namespace infrastructure { namespace ippacket { struct BuildIP; } }

/**
 * @namespace transport::udp
 * @brief Stateless UDP header construction utilities.
 *
 * These free functions operate on the shared @ref processing::PacketBuilder and
 * @ref infrastructure::ippacket::BuildIP abstractions, inserting UDP layer
 * metadata without owning any state themselves. The caller is responsible for
 * sequencing `reserveUDP` before `buildUdp`.
 */
namespace transport::udp
{

/**
 * @brief Reserves space for a UDP header in the given packet builder.
 *
 * Advances the packet builder's write cursor by the size of a UDP header so
 * that subsequent layers (payload, IP) can be appended or prepended without
 * reallocation.
 *
 * @param packetInfo The packet builder whose buffer will be advanced.
 * @param af         Address family (`IPv4` or `IPv6`), used to determine any
 *                   AF-specific alignment or pseudo-header requirements.
 */
void reserveUDP(processing::PacketBuilder& packetInfo, types::AddressFamily af);

/**
 * @brief Fills in the UDP header fields within an in-progress IP packet build.
 *
 * Writes the source port, destination port, length, and checksum fields into
 * the UDP header region previously reserved by @ref reserveUDP. The checksum
 * is computed from the IP pseudo-header embedded in @p ipBuild.
 *
 * @param af              Address family of the enclosing IP packet.
 * @param ipBuild         In-progress IP packet build context that holds the
 *                        pseudo-header and buffer.
 * @param sourcePort      UDP source port in host byte order.
 * @param destinationPort UDP destination port in host byte order.
 * @param fragmentOffset  IP fragment offset, used when the caller pre-fragments
 *                        large datagrams; defaults to 0 for unfragmented sends.
 *
 * @note Checksum computation accounts for the IPv4 or IPv6 pseudo-header
 * depending on @p af. Pass `fragmentOffset != 0` only for intermediate
 * fragments — the UDP length field is still set relative to the full payload.
 */
void buildUdp(
    types::AddressFamily af,
    infrastructure::ippacket::BuildIP& ipBuild,
    uint16_t sourcePort,
    uint16_t destinationPort,
    uint16_t fragmentOffset = 0
);

} // namespace transport::udp

#endif // UDP_H
