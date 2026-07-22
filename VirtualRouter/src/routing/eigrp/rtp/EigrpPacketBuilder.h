/**
 * @file EigrpPacketBuilder.h
 * @brief Stateless helpers for constructing EIGRP packet headers and TLV payloads.
 */

#ifndef EIGRP_PACKET_BUILDER_H
#define EIGRP_PACKET_BUILDER_H

#include <optional>
#include "packet/headers/EigrpHeader.hpp"
#include <cstdint>
#include <vector>

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }
namespace packet { class TLV16BufferManager; }
namespace config { class EigrpRegistry; }

namespace routing::eigrp
{
class EigrpInterface;
struct RouteInfo;
enum class TLVType : uint16_t;

/**
 * @brief Pure-static factory for assembling EIGRP packet headers and common TLVs.
 * @ingroup EIGRP_RTP
 *
 * `EigrpPacketBuilder` groups the stateless helper methods that @ref ReliableTransport
 * uses to build each packet type. It is responsible for the EIGRP common header
 * and for every TLV that is not a route (parameter, version, sequence, auth,
 * stub, multicast-sequence). Route TLVs are handled by @ref TLVBuilder.
 *
 * ## Architectural Role
 * `EigrpPacketBuilder` sits one level below @ref ReliableTransport in the
 * construction stack. `ReliableTransport::create*` methods call into this class
 * to write the EIGRP header and then append whatever TLVs are appropriate for
 * the packet type, before handing off to @ref TLVBuilder for route encoding.
 *
 * ## Lifecycle & Ownership
 * Not instantiable. All state lives in the caller-supplied `PacketBuilder` and
 * `TLV16BufferManager` objects. Callers own every buffer and are responsible
 * for committing or discarding the packet.
 *
 * @see ReliableTransport
 * @see TLVBuilder
 */
class EigrpPacketBuilder
{
public:
    /**
     * @brief Writes the EIGRP common header into a packet under construction.
     *
     * Reserves space for the fixed EIGRP header in `packet` and populates all
     * fields (opcode, AS number, virtual router ID, sequence, and ACK numbers).
     * Returns the header descriptor on success so callers can later finalize
     * checksum and length fields.
     *
     * @param packet Packet builder that receives the header bytes.
     * @param opcode EIGRP opcode identifying the packet type (UPDATE, QUERY, etc.).
     * @param seq    Outgoing sequence number; 0 for unreliable packets (HELLO, ACK).
     * @param ack    Sequence number being acknowledged; 0 if not piggybacking an ACK.
     * @param virId  Virtual router ID; identifies the EIGRP instance within a VRF.
     * @param asn    Autonomous System number; must match the peer's AS or the packet is dropped.
     * @return Populated @ref packet::EigrpHeader on success, or `std::nullopt` if the
     *         packet builder cannot allocate the required space.
     */
    static std::optional<packet::EigrpHeader> buildHeader(processing::PacketBuilder& packet,
        uint8_t opcode, uint32_t seq, uint32_t ack, uint16_t virId, uint32_t asn);

    /**
     * @brief Appends the parameter TLV carrying K-values and hold time.
     *
     * The parameter TLV is mandatory in every HELLO packet. Neighbors that
     * receive mismatched K-values will reject the adjacency.
     *
     * @param tlv  TLV buffer to append into.
     * @param iface Interface providing the K-value set and configured hold time.
     * @return True if the TLV was written successfully; false if the buffer is full.
     */
    static bool appendParameterTLV(packet::TLV16BufferManager& tlv, EigrpInterface& iface);

    /**
     * @brief Appends the software version TLV.
     *
     * Encodes this implementation's IOS-compatible major/minor version numbers.
     * Optional but expected by most EIGRP implementations.
     *
     * @param tlv TLV buffer to append into.
     * @return True if the TLV was written successfully.
     */
    static bool appendVersionTLV(packet::TLV16BufferManager& tlv);

    /**
     * @brief Appends the multicast-sequence TLV to a conditional-receive HELLO.
     *
     * This TLV embeds the sequence number of the preceding multicast reliable
     * packet. Neighbors listed in the accompanying sequence TLV must acknowledge
     * that sequence before multicast delivery resumes.
     *
     * @param tlv TLV buffer to append into.
     * @param seq Sequence number of the multicast reliable packet that triggered
     *            the conditional-receive HELLO.
     * @return True if the TLV was written successfully.
     */
    static bool appendMulticastSeqTLV(packet::TLV16BufferManager& tlv, uint32_t seq);

    /**
     * @brief Appends the authentication TLV if authentication is configured.
     *
     * Writes an MD5 or SHA-256 HMAC digest over the packet header and TLVs
     * encoded so far. Must be called last before packet finalization because
     * the digest covers preceding TLVs.
     *
     * @param tlv  TLV buffer to append into.
     * @param iface Interface providing the configured key chain and auth mode.
     */
    static void appendAuthTLV(packet::TLV16BufferManager& tlv, EigrpInterface& iface);

    /**
     * @brief Appends the stub TLV if this router is configured as a stub.
     *
     * Advertises stub capability flags so neighbors can suppress QUERY traffic
     * directed at this router. The TLV is omitted if stub mode is not enabled.
     *
     * @param tlv TLV buffer to append into.
     * @param configs Process-level EIGRP configuration registry containing the stub settings.
     * @return True if the TLV was written (stub is enabled); false if stub is off
     *         or the buffer is full.
     */
    static bool appendStubTLV(packet::TLV16BufferManager& tlv, const config::EigrpRegistry& configs);

    /**
     * @brief Appends one IP-address entry per neighbor into the sequence TLV.
     *
     * The sequence TLV in a conditional-receive HELLO lists the specific
     * neighbor IPs that must acknowledge the previous multicast before the
     * next one is sent. One TLV entry is written per address in `neighbors`.
     *
     * @param tlv       TLV buffer to append into.
     * @param neighbors IP addresses to include in the sequence TLV.
     * @return Number of address entries successfully written.
     */
    static size_t appendSequenceTLVs(packet::TLV16BufferManager& tlv, const std::vector<types::IPAddress>& neighbors);

    /**
     * @brief Encodes and appends route TLVs from the given route list.
     *
     * Delegates per-route encoding to @ref TLVBuilder. Stops early if the MTU
     * limit would be exceeded, allowing the caller to fragment across multiple
     * packets if needed.
     *
     * @param iface      Interface providing per-interface metric contributions.
     * @param tlv        TLV buffer to append into.
     * @param routes     Routes to encode.
     * @param bw         Composite bandwidth metric to apply to all encoded routes.
     * @param delay      Composite delay metric to apply to all encoded routes.
     * @param tlvVersion TLV encoding generation (classic or wide) to use.
     * @return Number of route TLVs successfully written into the buffer.
     */
    static size_t appendRoutes(EigrpInterface& iface, packet::TLV16BufferManager& tlv, const std::vector<const RouteInfo*>& routes, uint64_t bw, uint64_t delay, TLVType tlvVersion);
};
} // namespace routing::eigrp

#endif // EIGRP_PACKET_BUILDER_H
