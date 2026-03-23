// ReliableTransport.h

#ifndef EIGRP_RELIABLE_TRANSPORT_H
#define EIGRP_RELIABLE_TRANSPORT_H

#include <map>
#include <IPAddress.h>
#include <atomic>

#include "packet/TlvOptions.hpp"
#include "eigrp/rtp/Neighbor.h"

#define MAX_RETRANSMISSIONS 16

namespace processing { class PacketBuilder; }
class Internal_EigrpTest;

namespace routing::eigrp
{
class NeighborTable;

struct OutgoingQuery;
struct ActiveRoute;
struct RouteInfo;

class ReliableTransport
{
public:
    friend class Internal_EigrpTest;

    struct RTPInfo
    {
        RTPInfo(const packet::EigrpHeader& eigrp, const types::IPAddress& neighborIp) : eigrp(eigrp), neighborIp(neighborIp) {}
        const packet::EigrpHeader& eigrp;
        const types::IPAddress& neighborIp;
        std::vector<packet::TLV16Option> opts = {};
        Neighbor* neighbor = nullptr;
    };

    ReliableTransport(EigrpInterface& iface);
    ~ReliableTransport();

    enum class Resync { NONE, INIT, REPLY };

    void handleIncoming(const uint8_t* ipStart, const packet::EigrpHeader& eigrpPacket, const types::IPAddress& neighborIp, bool multicast);

    std::atomic<bool> pendingPeerTermination{false};

    void sendHello();
    void sendUnicastHello(const types::IPAddress& neighborIp);
    void sendConditionalHello(const std::vector<types::IPAddress>& neighbors, uint32_t seq);
    void sendAck(Neighbor& neighbor, uint32_t seqNum);
    void trackAck(Neighbor& neighbor, uint32_t);
    void attemptSendAck(Neighbor& neighbor, uint32_t);
    void sendNullUpdate(Neighbor& neighbor);
    void sendFullTopology(Neighbor& neighbor, Resync resync = Resync::NONE);
    void sendUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    void sendPoisenedUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    void sendQuery(const std::vector<ActiveRoute*>& routes);
    void sendUnicastQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes);
    void sendReply(Neighbor& neighbor, const std::vector<const RouteInfo*>& routes);
    void sendSIAQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes);
    void sendSIAReply(Neighbor& neighbor);
    
    bool validateSeqNum(RTPInfo& info, uint32_t seq);
    bool setupUnicastReliable(Neighbor& neighbor, packet::EigrpHeader& info);
    bool setupMulticastReliable(packet::EigrpHeader& info);

    void startMulticastReliable(MulticastReliablePacket& pkt, uint32_t seq);
    void startUnicastReliable(Neighbor& nbr, UnicastReliablePacket& pkt, uint32_t seq);
    void sendRetransmission(Neighbor& neighbor, packet::StaticHeader& header);
    void handleRetransmission(Neighbor* neighbor, MulticastReliablePacket& pkt, ReliableInfo& info, uint32_t seq);
    void handleRetransmission(Neighbor* neighbor, UnicastReliablePacket& pkt, uint32_t seq);

    uint32_t incrementSequenceNumber();
    uint32_t getSeq() { return nextSeq.load(std::memory_order_relaxed); }

    // Multicast Reliable
    std::map<uint32_t, MulticastReliablePacket> reliablePackets;



    struct PktInfo
    {
        uint64_t bandwidthMetric{0};
        uint64_t delay{0};
        TLVType version;
        uint16_t mtu;
        size_t sent{0};
    };

private:

    void transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest = nullptr);
    void transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::EigrpHeader& header);
    void releaseFailedPacket(processing::PacketBuilder& builder);
    void createPacket(processing::PacketBuilder& builder);

    std::optional<packet::EigrpHeader> createHello(processing::PacketBuilder& builder);
    std::optional<packet::EigrpHeader> createUnicastHello(processing::PacketBuilder& builder);
    std::optional<packet::EigrpHeader> createConditionalHello(processing::PacketBuilder& builder, PktInfo& info, const std::vector<types::IPAddress>& neighbors, uint32_t seq);
    std::optional<packet::EigrpHeader> createAck(processing::PacketBuilder& builder, uint32_t seq);
    std::optional<packet::EigrpHeader> createNullUpdate(processing::PacketBuilder& builder);
    std::optional<packet::EigrpHeader> createUpdate(processing::PacketBuilder& builder, PktInfo& info, Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    std::optional<packet::EigrpHeader> createQuery(processing::PacketBuilder& builder, PktInfo& info, const std::vector<ActiveRoute*>& queries);
    std::optional<packet::EigrpHeader> createUnicastQuery(processing::PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries);
    std::optional<packet::EigrpHeader> createReply(processing::PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<const RouteInfo*>& replies);
    std::optional<packet::EigrpHeader> createSIAQuery(processing::PacketBuilder& builder, PktInfo& info, const std::vector<OutgoingQuery*>& queries);
    std::optional<packet::EigrpHeader> createSIAReply(processing::PacketBuilder& builder);

    bool processConditionalReceive(uint32_t seq, Neighbor& neighbor);

    void processAck(Neighbor& neighbor, const uint32_t seq);
    void processHello(RTPInfo& info, bool unicast);
    void processUpdate(RTPInfo& info);
    void processQuery(RTPInfo& info);
    void processSIAQuery(RTPInfo& info);
    void processReply(RTPInfo& info);
    void processSIAReply(RTPInfo& info);

    void checkInit(Neighbor& neighbor);

    bool verifyNeighborAS(const packet::EigrpHeader& header);
    uint16_t getMtu();

    std::atomic<uint32_t> nextSeq = 1; ///< Next sequence number for packets.
    std::atomic<bool> firstFullSend = false;

    types::AddressFamily af;
    uint32_t as;

    NeighborTable* ntable = nullptr;
    EigrpInterface& iface;
};
} // namespace routing

#endif // EIGRP_RELIABLE_TRANSPORT_H

