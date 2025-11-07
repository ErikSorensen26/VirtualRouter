// ReliableTransport.h

#ifndef EIGRP_RELIABLE_TRANSPORT_H
#define EIGRP_RELIABLE_TRANSPORT_H

#include "EigrpTypes.hpp"
#include "Neighbor.h"

class PacketBuilder;

namespace Eigrp
{
class EigrpInterface;
class NeighborTable;
struct ReceivedRoute;
struct OutgoingQuery;

class ReliableTransport
{
public:
    struct RTPInfo
    {
        RTPInfo(const EigrpHeader& eigrp, const IPAddress& neighborIp) : eigrp(eigrp), neighborIp(neighborIp) {}
        const EigrpHeader& eigrp;
        const IPAddress& neighborIp;
        std::vector<TLV16Option> opts = {};
        Neighbor* neighbor = nullptr;
    };

    ReliableTransport(EigrpInterface& iface);

    void handleIncoming(const uint8_t* ipStart, const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast);

    void sendHello();
    void sendAck(Neighbor& neighbor, uint32_t sequenceNumber);
    void sendCondAck(Neighbor& neighbor, uint32_t sequenceNumber);
    void sendSequenceHello(const IPAddress& neighborIp, uint32_t seq);
    void sendUnicastHello(const IPAddress& neighborIp);
    void sendNullUpdate(Neighbor& neighbor);
    void sendFullTopology(Neighbor& neighbor);
    void sendUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& routes);
    void sendQuery(Neighbor* neighbor, const std::vector<OutgoingQuery*>& routes);
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

private:

    void transmit(PacketBuilder& pkt, const uint8_t* dest = nullptr);
    void transmitReliable(PacketBuilder& pkt, Neighbor* neighbor, EigrpHeader& header);
    void trackReliable(Neighbor* nbr, const IPAddress& ip, const PacketBuilder& pkt, uint32_t seq);

    void processAck(Neighbor& neighbor, const uint32_t seq);
    void processHello(RTPInfo& info, bool unicast);
    void processUpdate(RTPInfo& info);
    void processQuery(RTPInfo& info);
    void processSIAQuery(RTPInfo& info);
    void processReply(RTPInfo& info);
    void processSIAReply(RTPInfo& info);

    /*void sendHelloPacket(Neighbor* neighbor = nullptr, bool unicast = false, bool update = false, uint32_t sequenceNumber = 0);
    virtual void sendUpdate(Neighbor* neighbor, const std::vector<ReceivedRoute>& routes, EigrpConfigs::UpdateType updateType, bool restart = false, bool conditional = false, std::vector<IPAddress> conditionalNeighbors = {});
    virtual void sendQuery(Neighbor* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes);
    void sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes);
    void sendSIAQueryToNeighbor(Neighbor* neighbor, const IPAddress& neighborIp);
    virtual void sendReplyToNeighbor(Neighbor* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t sequenceNumber);
    void sendSIAReplyToNeighbor(Neighbor* neighbor, const IPAddress& neighborIp, uint32_t querySequence);*/

    void parseEigrpOptionHelper(const EigrpHeader& hdr, std::vector<TLV16Option>& options);
    bool verifyNeighborAS(const EigrpHeader& header);
    bool validateSequenceNumber(const EigrpHeader& eigrpHello);
    uint16_t getMtu();

    std::atomic<uint32_t> nextSeq = 1; ///< Next sequence number for packets.

    AddressFamily af;
    uint16_t as;

    NeighborTable* ntable = nullptr;
    EigrpInterface& iface;
    std::mutex bufferMutex;
};
}

#endif // EIGRP_RELIABLE_TRANSPORT_H
