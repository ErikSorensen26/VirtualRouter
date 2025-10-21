// EigrpPacketBuilder.hpp /** @brief Configures an EIGRP Hello packet with specific settings.

 /*
 * Constructs and sends a Hello packet to a neighbor, initiating or maintaining
 * the neighbor relationship. The Hello packet includes necessary information for
 * synchronization and state management between peers.
 *
 * @param eigrp Reference to the EIGRP header.
 * @param eigrpInt Pointer to the EIGRP interface.
 * @param neighborIp IP address of the neighbor.
 * @param sequenceNumber Sequence number for the Hello packet.
 * @param ack Indicates if this Hello is an ACK.
 * @param update Indicates if this Hello is part of an update.
 */
void Eigrp::eigrpHello(PacketBuilder& packet, EigrpInterface& eigrpInt, const uint8_t* neighborIp, uint32_t sequenceNumber,  bool ack, bool update)
{
    addressFamily == AddressFamily::IPv4
        ? IPPacket::reserveIpv4(eigrpInt.currentInterface, packet)
        : IPPacket::reserveIpv6(eigrpInt.currentInterface, packet);

    packet.reserveHeader(HeaderType::EIGRP, EigrpHeader::fixedSize);
    auto nextHeader = packet.nextBuildHeader();
    if (!nextHeader) return;

    // Create the EIGRP Ack packet
    EigrpHeader eigrp;
    eigrp.setBuffer(nextHeader->buffer);

    eigrp.raw->version = 0x02;
    eigrp.setOpcode(Variable::Eigrp::Type::hello);
    std::fill(eigrp.raw->checksum, eigrp.raw->checksum + 2, 0);
    eigrp.setFlagInit(false);
    eigrp.setFlagCondRecv(false);
    eigrp.setFlagRestart(false);
    eigrp.setFlagEndOfTable(false);
    eigrp.setSequence(0);
    eigrp.setAck(0);
    eigrp.setVirtualRouterId(virtualRouterID);
    eigrp.setAutonomousSystem(asNumber);

    // Construct TLVs
    packet.addTLVSize(addCommonTlvs(eigrp, eigrpInt, update, ack, neighborIp, sequenceNumber));
}

/**
 * @brief Configures an EIGRP Update packet with specific settings.
 *
 * Constructs and sends an Update packet containing routing information to neighbors.
 * The Update packet can carry various types of routing information based on the
 * specified parameters, facilitating route advertisement and query responses.
 *
 * @param eigrp Reference to the EIGRP header.
 * @param sequenceNum Sequence number for the Update packet.
 * @param neighbork Pointer to the neighbor that the packet is being sent to.
 * @param init Indicates if this Update is part of initialization.
 * @param conditional Indicates if this Update is conditional.
 * @param restart Indicates if this Update is part of a restart.
 * @param endoftable Indicates if this Update marks the end of the table.
 * @param query Indicates if this Update is a Query.
 * @param reply Indicates if this Update is a Reply to a Query.
 */
void Eigrp::eigrpUpdate(EigrpHeader &eigrp, uint32_t sequenceNum, bool init, bool conditional, bool restart, bool endoftable, bool query, bool reply)
{
    // Create an initiated eigrp update header
    eigrp.raw->version = 0x02;
    if (reply)
    {
        eigrp.setOpcode(Variable::Eigrp::Type::reply);
    }
    else if (query)
    {
        eigrp.setOpcode(Variable::Eigrp::Type::query);
    }
    else
    {
        eigrp.setOpcode(Variable::Eigrp::Type::update);
    }
    std::fill(eigrp.raw->checksum, eigrp.raw->checksum + 2, 0);
    eigrp.setFlagInit(false);
    eigrp.setFlagCondRecv(false);
    eigrp.setFlagRestart(false);
    eigrp.setFlagEndOfTable(false);
    eigrp.setSequence(sequenceNum);
    eigrp.setAck(0);
    eigrp.setVirtualRouterId(virtualRouterID);
    eigrp.setAutonomousSystem(asNumber);
}
