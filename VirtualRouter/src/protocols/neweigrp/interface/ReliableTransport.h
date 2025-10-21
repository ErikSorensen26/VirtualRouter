// ReliableTransport.h

#ifndef RELIABLE_TRANSPORT_H
#define RELIABLE_TRANSPORT_H

namespace Protocol
{
class ReliableTransport
{
public:
    struct PacketOptions
    {

    };

    void sendReliable();

    /**
     * @brief Processes an incoming EIGRP packet.
     *
     * Determines the type of the received EIGRP packet and dispatches it to the appropriate
     * handler function (e.g., Hello, Update, Query, Reply, ACK).
     *
     * @param eigrpPacket Pointer to the received EIGRP packet header.
     * @param neighborIp IP address of the neighbor that sent the packet.
     */
    void handleIncoming(const EigrpHeader& eigrpPacket, const uint8_t* neighborIp, bool multicast);

    /**
     * @brief Sends an ACK to a neighbor.
     *
     * Constructs and sends an ACK packet to confirm the receipt of a specific Update or Query packet.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param sequenceNumber Sequence number to acknowledge.
     */
    void sendAckToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t sequenceNumber);

    /**
     * @brief Retrieves the next sequence number for packet identification.
     *
     * Generates and returns the next available sequence number for EIGRP packet tracking,
     * ensuring uniqueness and proper sequencing of packets.
     *
     * @return Next sequence number.
     */
    uint32_t getNextSequenceNumber();

private:

    //combine

    /**
     * @brief Processes incoming Hello packets from a neighbor.
     *
     * Handles the reception of Hello packets, updating neighbor states, exchanging Router IDs,
     * and maintaining the neighbor relationship.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param receivedHello Pointer to the received Hello packet header.
     * @param neighborIp IP address of the neighbor.
     */
    void processHello(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedHello, const IPAddress& neighborIp, bool unicast);

    /**
     * @brief Processes incoming Update packets from a neighbor.
     *
     * Parses the Update packet, extracts routing information, updates the topology table,
     * and sends acknowledgements as necessary.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param receivedUpdate Pointer to the received Update packet header.
     */
    void processUpdate(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedUpdate);

    /**
     * @brief Processes buffered Update packets for a neighbor.
     *
     * Sends any buffered Update packets that were previously held due to pending acknowledgements
     * or state conditions.
     *
     * @param neighbor pointer to the neighbor's information.
     */
    void processBufferedPackets(EigrpConfigs::NeighborInfo* neighbor);

    /**
     * @brief Processes an incoming ACK from a neighbor.
     *
     * Validates the acknowledgement, removes the corresponding packet from the retransmission queue,
     * and updates RTT estimates.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param sequenceNumber Sequence number being acknowledged.
     */
    void processAck(EigrpConfigs::NeighborInfo* neighbor, const uint32_t sequenceNumber);

    /**
     * @brief Processes an incoming Query packet from a neighbor.
     *
     * Handles the Query by checking the feasibility of the routes in question and responding
     * with appropriate Reply packets.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param receivedQuery Pointer to the received Query packet header.
     * @param neighborIp IP of the neighbor that sent the query.
     */
    void processQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp);

    /**
     * @brief Processes an incoming SIAQuery packet from a neighbor
     * 
     * Handles the SIAQuery by checking the feasibility of the routes in question and immedietly
     * responsing with appropriate Reply packets.Arp
     * 
     * @param neighbor Pointer to the neighbor information.
     * @param receivedQuery Pointer to the received Query packet header.
     * @param neighborIp IP of the neighbor that sent the query.
     */
    void processSIAQuery(EigrpConfigs::NeighborInfo* neighbor, const EigrpHeader& receivedQuery, const IPAddress& neighborIp);

    /**
     * @brief Processes an incoming Reply packet from a neighbor.
     *
     * Updates the topology table based on the Reply, recalculates the best routes,
     * and resolves any pending queries.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param neighborIp Reference to neighbors IP.
     * @param recievedReply Pointer to the received Reply packet header.
     */
    void processReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& recievedReply);

    /**
     * @brief Processes an incoming SIAReply packet from a neighbor.
     *
     * Updates the topology table based on the SIAReply, recalculates the best routes,
     * and resolves any pending queries.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param neighborIp Reference to neighbors IP.
     * @param recievedReply Pointer to the received Reply packet header.
     */
    void processSIAReply(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, const EigrpHeader& recievedReply);
    /**
     * @brief Sends a Hello packet to a neighbor or multicast group.
     *
     * Constructs and dispatches a Hello packet to either a specific neighbor or the multicast
     * address, depending on the parameters. The sequence number aids in tracking and acknowledging
     * Hello packets.
     *
     * @param neighbor Pointer to the neighbor's information.
     * @param unicast True to send a unicast Hello, false for multicast.
     * @param update Indicates if this Hello is part of an update.
     * @param sequenceNumber Sequence number for the Hello packet.
     */
    void sendHelloPacket(EigrpConfigs::NeighborInfo* neighbor = nullptr, bool unicast = false, bool update = false, uint32_t sequenceNumber = 0);

    /**
     * @brief Sends an Update packet to a neighbor.
     *
     * Constructs and sends an Update packet containing routing information to the specified neighbor.
     * Handles different types of updates based on the updateType parameter.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param routes Routes to include in the update.
     * @param updateType Type of the update (FULL/QUERY/RESPONSE_QUERY/PARTIAL/TRIGGERED/WITHDRAW).
     * @param restart Indicates if this update is part of a restart.
     * @param conditional Indicates if this update is conditional.
     * @param conditionalNeighbors List of neighbors for conditional updates.
     */
    virtual void sendUpdate(EigrpConfigs::NeighborInfo* neighbor, const std::vector<EigrpConfigs::RoutingUpdate>& routes, EigrpConfigs::UpdateType updateType, bool restart = false, bool conditional = false, std::vector<IPAddress> conditionalNeighbors = {});

    /**
     * @brief Sends a Query packet to a specific neighbor.
     *
     * Directly queries a single neighbor about specific failed routes to ascertain their status
     * and potential alternatives.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param neighborIp Reference to neighbors IP address.
     * @param failedRoutes Routes that have failed and need to be queried.
     * @return the sequence number for the query.
     */
    virtual void sendQuery(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> failedRoutes);

    /**
     * @brief Sends a query to all connected neighbors
     *
     * Finds all connected neighbors and sends a query to all of them.
     *
     * @param failedRoutes routes that will be included in the queries.
     */
    void sendQueryToNeighbors(std::vector<RoutingTable::Eigrp*> failedRoutes);

    /**
     * @brief Sends a SIAQuery packet to a specific neighbor.
     *
     * Directly queries a single neighbor about specific failed routes to ascertain their status
     * and potential alternatives.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param neighborIp Reference to neighbors IP address.
     */
    void sendSIAQueryToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp);

    /**
     * @brief Sends a Reply packet to a neighbor in response to a Query.
     *
     * Responds to a neighbor's Query packet by providing detailed routing information about
     * the requested routes.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param neighborIp Reference to the neighbors IP.
     * @param queryRoutes Routes queried from the neighbor.
     * @param existingRoutes existing route to send to neighbor.
     * @param querySequence Outgoing query configs.
     */
    virtual void sendReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, std::vector<RoutingTable::Eigrp*> queryRoutes, std::vector<RoutingTable::Eigrp*> existingRoutes, uint32_t sequenceNumber);

    /**
     * @brief Sends a SIAReply packet to a neighbor in response to a Query.
     *
     * Responds to a neighbor's Query packet by providing detailed routing information about
     * the requested routes.
     *
     * @param neighbor Pointer to the neighbor information.
     * @param neighborIp Reference to the neighbors IP.
     * @param querySequence Query sequence number that the reply needs to match to.
     */
    void sendSIAReplyToNeighbor(EigrpConfigs::NeighborInfo* neighbor, const IPAddress& neighborIp, uint32_t querySequence);
}
}

#endif // RELIABLE_TRANSPORT_H
