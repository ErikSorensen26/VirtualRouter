/**
 * @file PacketDispatcherV2.h
 * @brief OSPFv2-specific packet dispatcher: encode, decode, and send OSPFv2 wire packets.
 */

/**
 * @defgroup OSPF_V2_TRANSMISSION OSPFv2 Transmission
 * @ingroup OSPF_V2
 * @brief OSPFv2 wire-format packet encoding, decoding, and sending.
 */

#ifndef V2_PACKET_DISPATCHER_H
#define V2_PACKET_DISPATCHER_H

#include "packet/headers/Ospfv2Header.hpp"
#include "ospf/interface/OspfInterface.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/transmission/PacketDispatcher.h"

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }
namespace packet { struct Ospfv2HelloHeader; struct Ospfv2DBDHeader; struct Ospfv2LSRHeader; struct Ospfv2LSAHeader; }

namespace routing::ospf
{
class OspfInterface;
class Neighbor;
class NeighborTable;

/**
 * @brief OSPFv2 concrete packet dispatcher.
 * @ingroup OSPF_V2_TRANSMISSION
 *
 * Implements all OSPFv2 wire-format encoding and decoding on top of the
 * @ref PacketDispatcher base. This includes IPv4-specific authentication
 * (simple password and MD5 cryptographic), the LLS Data Block extension
 * (RFC 4813), and all five OSPFv2 packet types.
 *
 * ## Architectural Role
 * One instance is created per OSPFv2 interface by `OspfInterface` during
 * initialisation. It delegates LSA body serialisation to the LSA type
 * structs in `ospfv2/database/`, and calls `transmit()` which hands the
 * finished packet to the underlying @ref processing::PacketBuilder.
 *
 * ## Concurrency Model
 * All methods run on the owning process's scheduler thread. No locking.
 *
 * @see PacketDispatcherV3, PacketDispatcher
 */
class PacketDispatcherV2 : public PacketDispatcher
{
public:
    /**
     * @brief Constructs the dispatcher and binds it to an OSPFv2 interface.
     *
     * @param iface   The OSPFv2 interface that owns this dispatcher.
     * @param configs Reference to the shared base interface configuration registry.
     */
    PacketDispatcherV2(OspfInterface& iface, config::Reference<config::OspfInterfaceBaseRegistry>& configs);

    config::OspfInterfaceBaseRegistry& getBaseConfigs() override;

    /**
     * @brief Dispatches an incoming OSPFv2 packet to the appropriate handler.
     *
     * Called by the ingress path after the outer IP header has been stripped.
     * Validates the common OSPFv2 header, authenticates the packet if required,
     * then routes it to `processHello`, `processDBD`, `processLSUpdate`, etc.
     *
     * @param ospfHeader  Decoded OSPFv2 common header.
     * @param neighborIp  Source IPv4 address of the packet (4-byte network-order).
     * @param multicast   True if the packet arrived on the AllSPFRouters multicast address.
     */
    void handleIncoming(const packet::Ospfv2Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

    void sendHello() override;
    void sendUnicastHello(Neighbor& nbr) override;
    void sendInitDBD(Neighbor& nbr) override;
    bool sendDBD(Neighbor& nbr) override;
    bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& records) override;

    bool sendLSRequest(Neighbor& nbr) override;
    bool sendLSUpdate(Neighbor* nbr) override;

    void onDbdRetransmissionTimer(Neighbor& nbr) override;

private:
    void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest = nullptr) override;
    bool processOptions(uint32_t options, Neighbor& nbr) override;

    /**
     * @brief Finalizes the OSPFv2 common header: sets length, authentication, and checksum.
     *
     * @param hdr     Header to finalize (modified in place).
     * @param builder Builder positioned just past the payload.
     * @param lls     True if an LLS Data Block was appended after the OSPF payload.
     */
    void finalizeHeader(packet::Ospfv2Header& hdr, OspfBuilder& builder, bool lls = false);

    /**
     * @brief Transmits a packet reliably (queues in the retransmission list for `neighbor`).
     *
     * @param pkt      Serialized packet.
     * @param neighbor Target neighbor, or null for multicast.
     * @param header   Reference to the OSPFv2 header embedded in `pkt` (for sequence stamping).
     */
    void transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::Ospfv2Header& header);

    /**
     * @brief Prepares a neighbor for a new DD exchange and builds the outgoing header stub.
     *
     * @param neighbor Neighbor entering or re-entering Exchange state.
     * @param pkt      Header to fill in.
     * @return True if setup succeeded and the packet should be sent.
     */
    bool setupDbd(Neighbor& neighbor, packet::Ospfv2Header& pkt);

    /// Returns the effective MTU for this interface, used to bound packet sizes.
    uint16_t getMtu();

    /// Allocates a builder and writes the OSPFv2 common header of the given type.
    std::optional<packet::Ospfv2Header> buildHeader(processing::PacketBuilder& builder, uint8_t type);
    /// Appends a Hello payload to `builder`; includes LLS block if `lls` is true.
    std::optional<packet::Ospfv2HelloHeader> buildHello(OspfBuilder builder, bool lls);
    /// Appends a DBD payload for the given neighbor.
    std::optional<packet::Ospfv2DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    /// Serializes a single LSA header from the LSDB into `builder`.
    std::optional<packet::Ospfv2LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction);
    /// Serializes a verbatim copy of an LSA header (no age recalculation).
    std::optional<packet::Ospfv2LSAHeader> buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

    /// Builds the full LSR packet for `nbr` and returns the builder on success.
    std::optional<processing::PacketBuilder> buildLSRequest(Neighbor& nbr);
    /// Builds the full LSU packet for `nbr` (or multicast if null) and returns the builder.
    std::optional<processing::PacketBuilder> buildLSUpdate(Neighbor* nbr);

    /// Appends LS Request entries for outstanding requests from `nbr`.
    size_t addLSRequests(OspfBuilder& builder, Neighbor& nbr);
    /// Appends LS Update entries from the neighbor's retransmission queue (or multicast queue).
    size_t addLSUpdates(OspfBuilder& builder, Neighbor* nbr);
    /// Appends LS Acknowledgement entries from `acks`.
    size_t addLSAcks(OspfBuilder& builder, std::span<LsaRecordRef>& acks);

    /// Fills the DD summary list into the builder for the neighbor's current exchange page.
    void buildDescriptions(OspfBuilder& builder, Neighbor& nbr);
    /// Serializes the body of an LSA record into the builder based on its type byte.
    bool buildLSABody(OspfBuilder& builder, LsaRecord& body, uint8_t type);

private:
    // RECEIVE PATH

    /// Processes an incoming Hello packet from a neighbor or candidate neighbor.
    void processHello(HeaderInfo& info, bool unicast);
    /// Processes an incoming Database Description packet.
    void processDBD(HeaderInfo& info);
    /// Processes an incoming LS Acknowledgement packet.
    void processLSAck(HeaderInfo& info);
    /// Processes an incoming LS Request packet.
    void processLSRequest(HeaderInfo& info);
    /// Processes an incoming LS Update packet.
    void processLSUpdate(HeaderInfo& info);

    /**
     * @brief Parses and validates the LLS Data Block that may follow an OSPFv2 packet.
     *
     * @param info Parsing context; `offset` is advanced past the LLS block on success.
     */
    void processLLSDataBlock(PacketDispatcher::HeaderInfo& info);

    // AUTHENTICATION BUILD

    /**
     * @brief Appends an LLS authentication TLV (MD5) to the packet.
     *
     * @param info    Builder context positioned at the end of the OSPF payload.
     * @param llsSize Byte size of the LLS block so far (used in HMAC).
     * @param seq     Cryptographic sequence number.
     * @param secret  Pointer to the 16-byte MD5 key.
     * @return True if the TLV was written successfully.
     */
    bool buildLLSAuthentication(OspfBuilder& info, uint16_t llsSize, uint32_t seq, uint8_t* secret);

    /**
     * @brief Writes the OSPFv2 simple (cleartext) authentication field into the header.
     *
     * @param header Header to modify.
     * @param secret 8-byte password packed as a 64-bit integer.
     */
    void buildOspfSimpleAuthentication(packet::Ospfv2Header& header, uint64_t secret);

    /**
     * @brief Appends the OSPFv2 cryptographic (MD5) authentication trailer.
     *
     * @param info   Builder context.
     * @param hdr    Header to stamp with the key ID and auth data length.
     * @param seq    Cryptographic sequence number.
     * @param id     Key ID.
     * @param secret Pointer to the 16-byte MD5 key.
     * @return True if the trailer was written successfully.
     */
    bool buildOspfCryptoAuthentication(OspfBuilder& info, packet::Ospfv2Header& hdr, uint32_t seq, uint8_t id, uint8_t* secret);

    // AUTHENTICATION VERIFY

    /**
     * @brief Validates the simple (cleartext) authentication field of a received packet.
     *
     * @param hdr Received OSPFv2 header.
     * @return True if the password matches the configured key.
     */
    bool processOspfSimpleAuthentication(const packet::Ospfv2Header& hdr);

    /**
     * @brief Validates the cryptographic (MD5) authentication trailer of a received packet.
     *
     * @param info Parsing context containing the raw packet bytes.
     * @param hdr  Received OSPFv2 header.
     * @return True if the HMAC is valid and the sequence number is acceptable.
     */
    bool processOspfCryptoAuthentication(HeaderInfo& info, const packet::Ospfv2Header& hdr);

    /**
     * @brief Deserializes an LSA body from a raw buffer using the given OSPFv2 LSA type byte.
     *
     * @param type  OSPFv2 LSA type (1=Router, 2=Network, 3=SummaryNetwork, etc.).
     * @param buf   Pointer to the LSA body bytes (after the 20-byte LSA header).
     * @param len   Length of the body in bytes.
     * @param lsId  Link State ID from the LSA header; required for opaque LSA parsing.
     * @return Populated `LsaBody` variant on success, `std::nullopt` if parsing fails.
     */
    std::optional<LsaBody> buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len, uint32_t lsId = 0);

    config::Reference<config::OspfInterfaceBaseRegistry> baseConfigs; ///< Version-independent interface config.
    config::Reference<config::OspfInterfaceRegistry> configs;         ///< OSPFv2-specific interface config.
};
} // namespace routing

#endif // V2_PACKET_DISPATCHER_H

