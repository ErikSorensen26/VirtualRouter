/**
 * @file PacketDispatcherV3.h
 * @brief OSPFv3-specific packet dispatcher: encode, decode, and send OSPFv3 wire packets.
 */

/**
 * @defgroup OSPF_V3_TRANSMISSION OSPFv3 Transmission
 * @ingroup OSPF_V3
 * @brief OSPFv3 wire-format packet building, sending, and reception.
 */

#ifndef V3_PACKET_DISPATCHER_H
#define V3_PACKET_DISPATCHER_H

#include "packet/headers/Ospfv3Header.hpp"
#include "ospf/interface/OspfInterfaceBase.h"
#include "ospf/database/LsdbTypes.hpp"
#include "ospf/transmission/PacketDispatcher.h"

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }
namespace packet { struct Ospfv3HelloHeader; struct Ospfv3DBDHeader; struct Ospfv3LSRHeader; struct Ospfv3LSAHeader; }

namespace routing::ospf
{

class OspfInterfaceBase;
class Neighbor;
class NeighborTable;

/**
 * @brief OSPFv3 concrete packet dispatcher.
 * @ingroup OSPF_V3_TRANSMISSION
 *
 * Implements all OSPFv3 wire-format encoding and decoding on top of the
 * @ref PacketDispatcher base. This includes IPv6 link-local addressing, the LLS
 * Data Block extension (RFC 4813), and all five OSPFv3 packet types.
 *
 * ## Architectural Role
 * One instance is created per OSPFv3 interface by `OspfInterfaceBase` during
 * initialisation. It delegates LSA body serialisation to the LSA type structs
 * in `ospfv3/database/`, and calls `transmit()` which hands the finished packet
 * to the underlying @ref processing::PacketBuilder.
 *
 * ## Concurrency Model
 * All methods run on the owning process's scheduler thread. No locking.
 *
 * @see PacketDispatcherV2, PacketDispatcher
 */
class PacketDispatcherV3 : public PacketDispatcher
{
    friend class ::Internal_OspfTest;
public:
    /**
     * @brief Constructs the dispatcher and binds it to an OSPFv3 interface.
     *
     * @param iface The OSPFv3 interface that owns this dispatcher.
     */
    PacketDispatcherV3(OspfInterfaceBase& iface);

    /**
     * @brief Dispatches an incoming OSPFv3 packet to the appropriate handler.
     *
     * Called by the ingress path after the outer IPv6 header has been stripped.
     * Validates the common OSPFv3 header, then routes it to `processHello`,
     * `processDBD`, `processLSUpdate`, etc.
     *
     * @param ospfHeader  Decoded OSPFv3 common header.
     * @param neighborIp  Source IPv6 address of the packet.
     * @param multicast   True if the packet arrived on an AllSPFRouters multicast address.
     */
    void handleIncoming(const packet::Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

    void sendHello() override;
    void sendUnicastHello(Neighbor& nbr) override;
    void sendInitDbd(Neighbor& nbr) override;
    bool sendDbd(Neighbor& nbr) override;
    bool sendLsAck(Neighbor& nbr, std::vector<LsaRecordRef>& records) override;

    bool sendLsr(Neighbor& nbr) override;
    bool sendLsu(Neighbor* nbr) override;

    void onDbdRetransmissionTimer(Neighbor& nbr) override;

private:
    void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest = nullptr) override;

    /**
     * @brief Finalizes the OSPFv3 common header: sets length and checksum.
     *
     * @param hdr     Header to finalize (modified in place).
     * @param builder Builder positioned just past the payload.
     * @param lls     True if an LLS Data Block was appended after the OSPF payload.
     * @param nbr     Target neighbor for a unicast send, or null for multicast; used
     *                to source the LLS resync/restart bits (multicast never sets resync).
     */
    void finalizeHeader(packet::Ospfv3Header& hdr, OspfBuilder& builder, bool lls = false, Neighbor* nbr = nullptr);

    /**
     * @brief Transmits a packet reliably (queues in the retransmission list for `neighbor`).
     *
     * @param pkt      Serialized packet.
     * @param neighbor Target neighbor, or null for multicast.
     * @param header   Reference to the OSPFv3 header embedded in `pkt` (for sequence stamping).
     */
    void transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::Ospfv3Header& header);

    /**
     * @brief Prepares a neighbor for a new DD exchange and builds the outgoing header stub.
     *
     * @param neighbor Neighbor entering or re-entering Exchange state.
     * @param pkt      Header to fill in.
     * @return True if setup succeeded and the packet should be sent.
     */
    bool setupDbd(Neighbor& neighbor, packet::Ospfv3Header& pkt);

    /// Returns the effective MTU for this interface, used to bound packet sizes.
    uint16_t getMtu();

    /// Allocates a builder and writes the OSPFv3 common header of the given type.
    std::optional<packet::Ospfv3Header> buildHeader(processing::PacketBuilder& builder, uint8_t type);
    /// Appends a Hello payload to `builder`; includes LLS block if `lls` is true.
    std::optional<packet::Ospfv3HelloHeader> buildHello(OspfBuilder& builder, bool lls);
    /// Appends a DBD payload for the given neighbor.
    std::optional<packet::Ospfv3DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    /// Serializes a single LSA header from the LSDB into `builder`.
    std::optional<packet::Ospfv3LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction);
    /// Serializes a verbatim copy of an LSA header (no age recalculation).
    std::optional<packet::Ospfv3LSAHeader> buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

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
    /// Serializes the body of an LSA record into the builder based on its type field.
    bool buildLSABody(OspfBuilder& builder, const LsaRecord& body, uint16_t type);

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
     * @brief Parses and validates the LLS Data Block that may follow an OSPFv3 packet.
     *
     * @param info Parsing context; `offset` is advanced past the LLS block on success.
     */
    void processLLSDataBlock(PacketDispatcher::HeaderInfo& info);

    /**
     * @brief Deserializes an LSA body from a raw buffer using the given OSPFv3 LSA function code.
     *
     * @param type OSPFv3 LSA type field (function code plus U/S1/S2 bits).
     * @param buf  Pointer to the LSA body bytes (after the 20-byte LSA header).
     * @param len  Length of the body in bytes.
     * @return Populated `LsaBody` variant on success, `std::nullopt` if parsing fails.
     */
    std::optional<LsaBody> buildLsaBody(uint16_t type, const uint8_t* buf, uint16_t len);
};

} // namespace routing::ospf

#endif // V3_PACKET_DISPATCHER_H
