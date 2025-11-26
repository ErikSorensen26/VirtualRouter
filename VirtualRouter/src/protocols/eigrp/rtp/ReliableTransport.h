// ReliableTransport.h

#ifndef EIGRP_RELIABLE_TRANSPORT_H
#define EIGRP_RELIABLE_TRANSPORT_H

#include "EigrpTypes.hpp"
#include "Neighbor.h"

#define MAX_RETRANSMISSIONS 16

class Internal_EigrpTest;
class PacketBuilder;
class Interface;

namespace Eigrp
{
class EigrpInterface;
class NeighborTable;
struct ReceivedRoute;
struct OutgoingQuery;
struct ActiveRoute;

class ReliableTransport
{
public:
    friend class ::Internal_EigrpTest;

    struct RTPInfo
    {
        RTPInfo(const EigrpHeader& eigrp, const IPAddress& neighborIp) : eigrp(eigrp), neighborIp(neighborIp) {}
        const EigrpHeader& eigrp;
        const IPAddress& neighborIp;
        std::vector<TLV16Option> opts = {};
        Neighbor* neighbor = nullptr;
    };

    ReliableTransport(EigrpInterface& iface);
    ~ReliableTransport();

    enum class Resync { NONE, INIT, REPLY };

    void handleIncoming(const uint8_t* ipStart, const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast);

    std::atomic<bool> pendingPeerTermination{false};

    void sendHello();
    void sendUnicastHello(const IPAddress& neighborIp);
    void sendSequenceHello(const IPAddress& neighborIp, uint32_t seq);
    void sendAck(Neighbor& neighbor, uint32_t seqNum);
    void sendCondAck(Neighbor& neighbor, uint32_t seqNum);
    void sendNullUpdate(Neighbor& neighbor);
    void sendFullTopology(Neighbor& neighbor, Resync resync = Resync::NONE);
    void sendUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    void sendPoisenedUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    void sendQuery(const std::vector<ActiveRoute*>& routes);
    void sendUnicastQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes);
    void sendReply(Neighbor& neighbor, const std::vector<const RouteInfo*>& routes, uint32_t seq);
    void sendSIAQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes);
    void sendSIAReply(Neighbor& neighbor, uint32_t seq);
    
    bool validateSeqNum(RTPInfo& info, uint32_t seq);
    bool setupReliablePacket(Neighbor* neighbor, EigrpHeader& info);


    void startMulticastReliable(MulticastReliablePacket& pkt, uint32_t seq);
    void startUnicastReliable(Neighbor& nbr, UnicastReliablePacket& pkt, uint32_t seq);
    void handleRetransmission(Neighbor* neighbor, MulticastReliablePacket& pkt, ReliableInfo& info, uint32_t seq);
    void handleRetransmission(Neighbor* neighbor, UnicastReliablePacket& pkt, uint32_t seq);

    uint32_t incrementSequenceNumber();
    uint32_t getSeq() { return nextSeq.load(std::memory_order_relaxed); }

    // Multicast Reliable
    std::mutex reliableMtx;
    std::atomic<uint32_t> currentReliable{0};
    std::map<uint32_t, std::pair<std::unordered_set<IPAddress>, MulticastReliablePacket>> reliableQueue;

    struct PktInfo
    {
        uint64_t bandwidthMetric{0};
        uint64_t delay{0};
        bool authentication{false};
        TLVType version;
        uint16_t mtu;
        size_t sent{0};
    };

private:

    void transmit(PacketBuilder& pkt, const uint8_t* dest = nullptr);
    void transmitReliable(PacketBuilder& pkt, Neighbor* neighbor, EigrpHeader& header);
    void trackReliable(Neighbor* nbr, const IPAddress& ip, const PacketBuilder& pkt, uint32_t seq);

    void releaseFailedPacket(PacketBuilder& builder);
    void createPacket(PacketBuilder& builder);

    std::optional<EigrpHeader> createHello(PacketBuilder& builder);
    std::optional<EigrpHeader> createUnicastHello(PacketBuilder& builder, const IPAddress& neighborIp);
    std::optional<EigrpHeader> createSequenceHello(PacketBuilder& builder, const IPAddress& neighborIp, uint32_t seq);
    std::optional<EigrpHeader> createAck(PacketBuilder& builder, Neighbor& neighbor, uint32_t seq);
    std::optional<EigrpHeader> createNullUpdate(PacketBuilder& builder, Neighbor& neighbor);
    std::optional<EigrpHeader> createUpdate(PacketBuilder& builder, PktInfo& info, Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    std::optional<EigrpHeader> createQuery(PacketBuilder& builder, PktInfo& info, const std::vector<ActiveRoute*>& queries);
    std::optional<EigrpHeader> createUnicastQuery(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries);
    std::optional<EigrpHeader> createReply(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<const RouteInfo*>& replies, uint32_t seq);
    std::optional<EigrpHeader> createSIAQuery(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries);
    std::optional<EigrpHeader> createSIAReply(PacketBuilder& builder, Neighbor& neighbor, uint32_t seq);

    void processAck(Neighbor& neighbor, const uint32_t seq);
    void processHello(RTPInfo& info, bool unicast);
    void processUpdate(RTPInfo& info);
    void processQuery(RTPInfo& info);
    void processSIAQuery(RTPInfo& info);
    void processReply(RTPInfo& info);
    void processSIAReply(RTPInfo& info);

    void parseEigrpOptionHelper(const EigrpHeader& hdr, std::vector<TLV16Option>& options);
    bool verifyNeighborAS(const EigrpHeader& header);
    bool validateSequenceNumber(const EigrpHeader& eigrpHello);
    uint16_t getMtu();

    std::atomic<uint32_t> nextSeq = 1; ///< Next sequence number for packets.

    AddressFamily af;
    uint32_t as;

    NeighborTable* ntable = nullptr;
    EigrpInterface& iface;
    std::mutex bufferMutex;
};
}

#endif // EIGRP_RELIABLE_TRANSPORT_H
