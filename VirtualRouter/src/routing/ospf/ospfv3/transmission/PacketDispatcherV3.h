/**
 * @file PacketDispatcherV3.h
 * @brief OSPFv3 packet transmission and reception for a single interface.
 *
 * Handles all aspects of OSPFv3 packet building, sending, and reception.
 * Supports Hello, DBD, LSRequest, LSUpdate, and LSAck messages, including
 * neighbor-specific transmission and retransmissions.
 */

/**
 * @defgroup OSPF_V3_TRANSMISSION OSPFv3 Transmission
 * @ingroup OSPF_V3
 * @brief OSPFv3 wire-format packet building, sending, and reception.
 */

#ifndef V3_PACKET_DISPATCHER_H
#define V3_PACKET_DISPATCHER_H

#include "packet/headers/Ospfv3Header.hpp"
#include "ospf/interface/OspfInterface.h"
#include "ospf/database/LSDB.hpp"
#include "ospf/transmission/PacketDispatcher.h"

namespace processing { class PacketBuilder; }
namespace types { struct IPAddress; }
namespace packet { struct Ospfv3HelloHeader; struct Ospfv3DBDHeader; struct Ospfv3LSRHeader; struct Ospfv3LSAHeader; }

namespace routing::ospf
{

class OspfInterface;
class Neighbor;
class NeighborTable;

/**
 * @brief Dispatches OSPFv3 packets for a specific interface.
 *
 * Responsible for packet building, transmission, reception, retransmission,
 * and integration with the neighbor table and LSDB.
 */
class PacketDispatcherV3 : public PacketDispatcher
{
    friend class ::Internal_OspfTest;
public:
    /**
     * @brief Constructs a PacketDispatcherV3 for a given interface.
     *
     * Initializes interface reference, base configuration registry, and
     * interface-specific configuration handles.
     *
     * @param iface The interface this dispatcher operates on.
     * @param configs Reference to the OspfInterfaceBaseRegistry.
     */
    PacketDispatcherV3(OspfInterface& iface);

    /**
     * @brief Finds and returns the base interface configuration registry.
     *
     * Provides access to static interface base configurations for protocol
     * operations, e.g., timers and retransmission settings.
     *
     * @return Reference to the OspfInterfaceBaseRegistry.
     */
    config::OspfInterfaceBaseRegistry& getConfigs() override;

    /**
     * @brief Handles an incoming OSPFv3 packet.
     *
     * Dispatches to the appropriate processing function depending on the
     * packet type (Hello, DBD, LSRequest, LSUpdate, LSAck).
     *
     * @param ospfHeader Reference to the parsed OSPFv3 header.
     * @param neighborIp Pointer to the sender's IP address.
     * @param multicast True if the packet was received via multicast.
     */
    void handleIncoming(const packet::Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast);

    /**
     * @brief Sends a Hello message on the interface.
     *
     * Used for neighbor discovery and DR/BDR election. May be multicast or
     * unicast depending on context.
     */
    void sendHello() override;

    /**
     * @brief Sends a unicast Hello message to a specific neighbor.
     *
     * Used when directly addressing a neighbor, typically during adjacency
     * establishment.
     *
     * @param nbr The neighbor to which the Hello is sent.
     */
    void sendUnicastHello(Neighbor& nbr) override;

    /**
     * @brief Sends the initial Database Description (DBD) packet to a neighbor.
     *
     * Starts the exchange of link-state information after adjacency
     * establishment.
     *
     * @param nbr The neighbor to which the DBD is sent.
     */
    void sendInitDBD(Neighbor& nbr) override;

    /**
     * @brief Sends a DBD packet to a neighbor.
     *
     * Returns true if the packet was successfully transmitted; false if
     * the neighbor cannot accept a DBD at this time.
     *
     * @param nbr The neighbor to send the DBD to.
     * @return True if DBD transmission succeeds.
     */
    bool sendDBD(Neighbor& nbr) override;

    /**
     * @brief Sends an LSAck message to a neighbor.
     *
     * Acknowledges received LSAs to reduce retransmission. Batch of LSA
     * references is included.
     *
     * @param nbr Neighbor to acknowledge LSAs to.
     * @param records Vector of LSA records being acknowledged.
     * @return True if transmission succeeds.
     */
    bool sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& records) override;

    /**
     * @brief Sends an LSRequest message to a neighbor.
     *
     * Requests missing LSAs for synchronization.
     *
     * @param nbr The neighbor to request LSAs from.
     * @return True if transmission succeeds.
     */
    bool sendLSRequest(Neighbor& nbr) override;

    /**
     * @brief Sends an LSUpdate message to a neighbor or flood list.
     *
     * Used to advertise new or updated LSAs.
     *
     * @param nbr Optional neighbor to send unicast; nullptr for flood.
     * @return True if transmission succeeds.
     */
    bool sendLSUpdate(Neighbor* nbr) override;

    /**
     * @brief Handles DBD retransmission timer expiration for a neighbor.
     *
     * Retransmits the last DBD packet to ensure reliable database description
     * delivery.
     *
     * @param nbr Neighbor whose timer fired.
     */
    void onDbdRetransmissionTimer(Neighbor& nbr) override;

private:
    // Internal helpers and builders...
    void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest = nullptr) override;
    bool processOptions(uint32_t options, Neighbor& nbr) override;

    void finalizeHeader(packet::Ospfv3Header& hdr, OspfBuilder& builder, bool lls = false);
    void transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::Ospfv3Header& header);
    bool setupDbd(Neighbor& neighbor, packet::Ospfv3Header& pkt);
    uint16_t getMtu();

    std::optional<packet::Ospfv3Header> buildHeader(processing::PacketBuilder& builder, uint8_t type);
    std::optional<packet::Ospfv3HelloHeader> buildHello(OspfBuilder& builder, bool lls);
    std::optional<packet::Ospfv3DBDHeader> buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls);
    std::optional<packet::Ospfv3LSAHeader> buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction);
    std::optional<packet::Ospfv3LSAHeader> buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record);

    std::optional<processing::PacketBuilder> buildLSRequest(Neighbor& nbr);
    std::optional<processing::PacketBuilder> buildLSUpdate(Neighbor* nbr);

    size_t addLSRequests(OspfBuilder& builder, Neighbor& nbr);
    size_t addLSUpdates(OspfBuilder& builder, Neighbor* nbr);
    size_t addLSAcks(OspfBuilder& builder, std::span<LsaRecordRef>& acks);

    void buildDescriptions(OspfBuilder& builder, Neighbor& nbr);
    bool buildLSABody(OspfBuilder& builder, LsaRecord& body, uint8_t type);

    void processHello(HeaderInfo& info, bool unicast);
    void processDBD(HeaderInfo& info);
    void processLSAck(HeaderInfo& info);
    void processLSRequest(HeaderInfo& info);
    void processLSUpdate(HeaderInfo& info);
    void processLLSDataBlock(PacketDispatcher::HeaderInfo& info);

    std::optional<LsaBody> buildLsaBody(uint16_t type, const uint8_t* buf, uint16_t len);
};

} // namespace routing::ospf

#endif // V3_PACKET_DISPATCHER_H
