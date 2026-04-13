/**
 * @file PacketDispatcher.h
 * @brief Abstract base for OSPF per-interface packet send/receive dispatch.
 */

/**
 * @defgroup OSPF_TRANSMISSION OSPF Transmission
 * @ingroup OSPF
 * @brief Shared packet dispatcher base, Fletcher checksum, and wire-format packet helpers.
 */

#ifndef PACKET_DISPATCHER_H
#define PACKET_DISPATCHER_H

#include "configs/registry/router/OspfInterfaceRegistry.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/neighbor/RetransmissionList.hpp"

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }

namespace routing::ospf
{
class UnicastPacket;
class OspfInterface;
class Neighbor;
class NeighborTable;

/**
 * @brief Abstract base class for per-interface OSPF packet send/receive dispatch.
 * @ingroup OSPF_TRANSMISSION
 *
 * `PacketDispatcher` centralises all packet I/O for one OSPF interface: Hello
 * generation, DD exchange, LSR/LSU/LSAck send, retransmission timers, and LLS
 * processing. The version-specific subclasses (@ref PacketDispatcherV2,
 * @ref PacketDispatcherV3) implement wire-format encoding/decoding.
 *
 * Each dispatcher is owned by an @ref OspfInterface and operates exclusively on
 * the owning process's scheduler thread — there is no additional locking.
 *
 * ## Architectural Role
 * Sits at the boundary between the OSPF state machines (neighbor FSM, flooding)
 * and the raw packet I/O layer (@ref processing::PacketBuilder). It knows about
 * individual neighbors but not about areas or the LSDB directly.
 *
 * ## Lifecycle & Ownership
 * Constructed by `OspfInterface` after the underlying hardware interface is
 * attached. Destroyed when the interface is removed. No explicit `stop()` is
 * required; timer handles are cancelled by the scheduler on destruction.
 *
 * ## Concurrency Model
 * All public methods must be called from the owning process's scheduler thread.
 * The retransmission timers fire on the same scheduler, so no additional
 * synchronization is needed.
 *
 * @see PacketDispatcherV2, PacketDispatcherV3, OspfInterface
 */
class PacketDispatcher
{
public:
    /**
     * @brief Constructs the dispatcher and binds it to an interface.
     * @ingroup OSPF_TRANSMISSION
     *
     * @param iface The OSPF interface that owns this dispatcher.
     */
    PacketDispatcher(OspfInterface& iface);

    /**
     * @brief Destroys the dispatcher and releases all retransmission state.
     */
    virtual ~PacketDispatcher();

    /// Returns the base (version-independent) interface config registry.
    virtual config::OspfInterfaceBaseRegistry& getConfigs() = 0;

    /// Sends a multicast Hello on the interface.
    virtual void sendHello() = 0;

    /**
     * @brief Sends a unicast Hello directly to a specific neighbor.
     *
     * Used during graceful restart and when the neighbor has not yet been seen
     * on the multicast Hello.
     *
     * @param nbr Target neighbor.
     */
    virtual void sendUnicastHello(Neighbor& nbr) = 0;

    /**
     * @brief Sends the initial empty DD packet to start the DD exchange.
     *
     * @param nbr Neighbor that has just entered ExStart state.
     */
    virtual void sendInitDBD(Neighbor& nbr) = 0;

    /**
     * @brief Sends the next DD packet in the database description exchange.
     *
     * @param nbr  Neighbor in the Exchange state.
     * @return True if there are more DD packets to send, false if this was the last.
     */
    virtual bool sendDBD(Neighbor& nbr) = 0;

    /**
     * @brief Sends LS Acknowledgements for a set of received LSAs.
     *
     * @param nbr     Neighbor to acknowledge toward.
     * @param records LSA records to acknowledge.
     * @return True if the packet was sent successfully.
     */
    virtual bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& ) = 0;

    /**
     * @brief Enqueues LSRs for reliable delivery and sends the first burst.
     *
     * @param nbr  Target neighbor.
     * @param dbds Keys of LSAs the neighbor has that we need.
     */
    void sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds);

    /**
     * @brief Enqueues LSUs for reliable delivery and sends the first burst.
     *
     * @param nbr  Target neighbor (null = flood to all Full neighbors).
     * @param keys LSA records to flood with associated flood-control metadata.
     */
    void sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys);

    /**
     * @brief Called when the DD retransmission timer fires for a neighbor.
     *
     * @param nbr Neighbor whose DD retransmit timer expired.
     */
    virtual void onDbdRetransmissionTimer(Neighbor& nbr) = 0;

    /**
     * @brief Retransmits outstanding LSUs for a neighbor whose LSU timer fired.
     *
     * @param nbr Neighbor whose LSU retransmit timer expired.
     */
    void onLsuRetransmissionTimer(Neighbor& nbr);

    /**
     * @brief Retransmits outstanding LSRs for a neighbor whose LSR timer fired.
     *
     * @param nbr Neighbor whose LSR retransmit timer expired.
     */
    void onLsrRetransmissionTimer(Neighbor& nbr);

    /**
     * @brief Sends the next paced LSU burst for a neighbor (or multicast if null).
     *
     * @param nbr Target neighbor, or null for the multicast LSU queue.
     */
    void onLsuPacingTimer(Neighbor* nbr);

    /**
     * @brief Sends the next paced LSR burst for a neighbor.
     *
     * @param nbr Target neighbor.
     */
    void onLsrPacingTimer(Neighbor& nbr);

    /// Returns the multicast LSU retransmission list for this interface.
    RetransmissionList<LsaKey, LsaRecordRef>& getMulticastLsu() { return multicastLsus; }

protected:
    /**
     * @brief Sends an LS Request packet to the given neighbor.
     *
     * @param nbr Neighbor to request from.
     * @return True if the packet was sent.
     */
    virtual bool sendLSRequest(Neighbor& nbr) = 0;

    /**
     * @brief Sends an LS Update packet, unicast to a neighbor or multicast if null.
     *
     * @param nbr Target neighbor, or null to multicast to all Full neighbors.
     * @return True if at least one LSA was included in the update.
     */
    virtual bool sendLSUpdate(Neighbor* nbr) = 0;

    /**
     * @brief Transmits a finalized packet via the underlying interface.
     *
     * @param pkt  Fully serialized OSPF packet.
     * @param dest Destination IP address, or null to send to the OSPF multicast address.
     */
    virtual void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest) = 0;

    /**
     * @brief Validates and applies received OSPF options for a neighbor.
     *
     * Called when a Hello or DD packet is received. Returns false if the options
     * are incompatible and the packet should be dropped.
     *
     * @param options Options bitmask from the received packet.
     * @param nbr     Neighbor the packet was received from.
     * @return False if the options mismatch makes the packet invalid.
     */
    virtual bool processOptions(uint32_t options, Neighbor& nbr) = 0;

    /**
     * @brief Computes the LSA age to advertise, respecting the DoNotAge bit for flood reduction.
     *
     * @param floodReduction True if flood reduction is active on this interface.
     * @param record         LSA record whose age to compute.
     * @return Encoded LSA age value to place in the header.
     */
    uint16_t calculateAge(bool floodReduction, const LsaRecord& record);

    /**
     * @brief Appends an LLS Data Block extension to the packet buffer.
     *
     * @param buf     Pointer past the end of the OSPF header where LLS data begins.
     * @param restart True if the graceful-restart extended option should be included.
     * @return Total byte length of the appended LLS block.
     */
    uint16_t addLinkLocalExtension(uint8_t* buf, bool restart);

    /**
     * @brief Writes the LLS Data Block checksum into the buffer in-place.
     *
     * @param buf Pointer to the start of the LLS Data Block (two-byte checksum field first).
     */
    void addLinkLocalChecksum(uint8_t* buf);

    /**
     * @brief Cursor into a @ref processing::PacketBuilder used while assembling OSPF packets.
     *
     * Wraps the builder with a current write offset and a maximum-size boundary
     * so that each build helper can check for space without knowing the full
     * packet layout.
     */
    struct OspfBuilder
    {
        processing::PacketBuilder& pkt; ///< Underlying packet buffer.
        uint8_t* buf = nullptr;         ///< Base pointer of the writable region.
        size_t offset;                  ///< Current write position relative to buf.
        size_t maxSize;                 ///< Maximum allowed offset (MTU-derived).

        /// Returns true if @p s more bytes can be written at the current offset.
        bool hasRoom(size_t s)
            { return (offset + s) <= maxSize; }
        /// Returns a pointer to the current write position.
        uint8_t* getBuf()
            { return buf + offset; }
    };

    /**
     * @brief Parsed context for an incoming OSPF packet passed through the processing chain.
     * @ingroup OSPF_TRANSMISSION
     *
     * Populated by the version-specific `handleIncoming()` before individual
     * packet-type handlers (processHello, processDBD, etc.) are called. The
     * handlers advance `offset` as they consume the payload.
     */
    struct HeaderInfo
    {
        /**
         * @brief Constructs a HeaderInfo from the fields extracted from the wire header.
         *
         * @param pload     Pointer to the payload region (after the common OSPF header).
         * @param psize     Total received packet size in bytes.
         * @param len       Payload length declared in the OSPF header.
         * @param authType  Authentication type from the OSPF header.
         * @param neighborIp Source IP of the packet.
         * @param rid       Router ID from the OSPF header.
         */
        HeaderInfo(uint8_t* pload, size_t psize, uint16_t len, uint8_t authType, const types::IPAddress& neighborIp, const uint32_t rid)
            : rid(rid), packetSize(psize), payloadSize(len), payload(pload), authType(authType), neighborIp(neighborIp) {}

        uint32_t rid;                   ///< Router ID from the received OSPF common header.
        const size_t packetSize{0};     ///< Actual received packet size in bytes (including IP payload).
        const size_t payloadSize{0};    ///< Payload length declared in the OSPF header.
        size_t offset{0};               ///< Current parse offset relative to the start of `payload`.
        uint8_t* payload{nullptr};      ///< Pointer to payload data starting after the common OSPFv2 Header.
        uint8_t authType{0};            ///< Authentication type field from the received OSPF header.
        uint8_t authSize{0};            ///< Byte length of the trailing authentication data; used by LLS to locate its block.

        const types::IPAddress& neighborIp;    ///< Sender IP.
        Neighbor* neighbor = nullptr;   ///< Neighbor object.
    };

    RetransmissionList<LsaKey, LsaRecordRef> multicastLsus; ///< Outstanding multicast LSUs awaiting implicit acknowledgement.

    OspfInterface& iface;    ///< Owning OSPF interface.
    NeighborTable& ntable;   ///< Neighbor table for this interface.
    types::AddressFamily af; ///< Address family (IPv4 or IPv6) of this interface.
};
} // namespace routing

#endif // PACKET_DISPATCHER_H

