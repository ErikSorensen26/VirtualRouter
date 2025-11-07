// ReliableTX.cpp

//TODO add int32 support for eigrp
#include "ReliableTransport.cpp"

namespace Eigrp
{
void ReliableTransport::transmit(PacketBuilder& pkt, const uint8_t* dest)
{
    const uint8_t* target = dest ? dest :
        af == AddressFamily::IPv4
            ? Variable::Multicast::Eigrp::address
            : Variable::Multicast::Eigrp::addressv6;

    auto* interface = iface.getIface();
    Protocol::IPPacket::BuildIP build = {
        .iface = interface,
        .packetInfo = pkt,
        .destIp = target,
        .DSCP = iface.configs->DSCP.load(std::memory_order_relaxed),
        .protocolType = Variable::IP::eigrp
    };

    af == AddressFamily::IPv4
        ? Protocol::IPPacket::buildIpv4(build)
        : Protocol::IPPacket::buildIpv6(build);
}

void ReliableTransport::transmitReliable(PacketBuilder& pkt, Neighbor* neighbor, EigrpHeader& header)
{
    if (!neighbor)
    {
        std::shared_lock<std::shared_mutex> lock(ntable->neighborMutex);
        for (const auto& [ip, nbr] : ntable->neighbors)
        {
            setupReliablePacket(nbr, header);
        }
    }
    else
    {
        if (setupReliablePacket(neighbor, header))
            transmit(pkt, neighbor->ipAddress.raw);
    }
}

void ReliableTransport::sendSequenceHello(const IPAddress& neighborIp, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(pkt,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), pkt.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendSequenceTLV(opts, neighborIp);
    EigrpPacketBuilder::appendMulticastSeqTLV(opts, seq);
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    pkt.addTLVSize(opts.size());

    transmit(pkt, neighborIp.raw);
}

void ReliableTransport::sendHello()
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(pkt,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), pkt.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    pkt.addTLVSize(opts.size());

    transmit(pkt);
}

void ReliableTransport::sendUnicastHello(const IPAddress& neighborIp)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(pkt,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), pkt.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    pkt.addTLVSize(opts.size());

    transmit(pkt, neighborIp.raw);
}

void ReliableTransport::sendAck(Neighbor& neighbor, uint32_t seq)
{
    if (neighbor.getState() != Neighbor::State::LOADING) return;

    PacketBuilder pkt(iface.getIface());
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(pkt,
        Variable::Eigrp::Type::hello,
        0, seq,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return;

    pkt.addTLVSize(0);
    transmit(pkt, neighbor.ipAddress.raw);
}

void ReliableTransport::sendCondAck(Neighbor& neighbor, uint32_t seq)
{
    //TODO
}

void ReliableTransport::sendNullUpdate(Neighbor& neighbor)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto* interface = iface.getIface();

    PacketBuilder pkt(interface);
    af == AddressFamily::IPv4
        ? Protocol::IPPacket::reserveIpv4(interface, pkt)
        : Protocol::IPPacket::reserveIpv6(interface, pkt);

    auto& base = iface.getBase();
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(pkt,
        Variable::Eigrp::Type::update,
        incrementSequenceNumber(),
        0,
        base.getVirtualRouterID(),
        base.getAS()
    );
    if (!eigrpHeader) return;
    eigrpHeader->setFlagInit(true);

    uint16_t mtuSize = getMtu();
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), pkt.getMaxHeaderSize(mtuSize));
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    // restart?

    pkt.addTLVSize(opts.size());

    transmitReliable(pkt, &neighbor, *eigrpHeader);
}

void ReliableTransport::sendFullTopology(Neighbor& neighbor)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto& interface = iface.getIface();
    auto& base = iface.getBase();

    uint32_t bandwidthMetric = (10000000 / interface.configs.bandwidth.load(std::memory_order_relaxed));
    uint32_t delay = interface.configs.delay.load(std::memory_order_relaxed);
    bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    auto topology = iface.getTopController();
    std::vector<const RouteInfo*> allRoutes = topology.filterAdvertisableRoutes(topology.getAllRoutes());
    if (allRoutes.empty()) return;

    const uint16_t mtuSize = getMtu();
    size_t sendCount = 0;
    bool first = true;

    do
    {
        PacketBuilder eigrpPacket(&interface);
        af == AddressFamily::IPv4
            ? Protocol::IPPacket::reserveIpv4(&interface, eigrpPacket)
            : Protocol::IPPacket::reserveIpv6(&interface, eigrpPacket);

        uint32_t seqNum = incrementSequenceNumber();
        std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(eigrpPacket, Variable::Eigrp::Type::update, seqNum, 0, iface.getBase().getVirtualRouterID(), as);
        if (!eigrp)
        {
            iface.getIface().tx->release(eigrpPacket.frame);
            return;
        }

        eigrp->setFlagInit(first);
        uint16_t maxRouteSize = mtuSize - static_cast<uint16_t>(eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));

        TLV16BufferManager opts(eigrp->getTrail().data(), maxRouteSize);

        EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());
        
        const std::vector<const RouteInfo*> remaining = std::vector<const RouteInfo*>(allRoutes.begin() + static_cast<int>(sendCount), allRoutes.end());
        sendCount += EigrpPacketBuilder::appendRoutes(opts, remaining, bandwidthMetric, delay, neighbor.tlvType);

        if (sendCount >= allRoutes.size() && allRoutes.size() > 0)
            eigrp->setFlagEndOfTable(true);

        if (authentication)
        {
            opts.addLen(eigrpPacket.getMaxHeaderSize(mtuSize) - opts.size() - EigrpHeader::fixedSize);
            EigrpPacketBuilder::appendAuthTLV(opts, iface);
        }
        eigrpPacket.addTLVSize(opts.size());

        transmitReliable(eigrpPacket, &neighbor, *eigrp);
        first = false;
    }
    while (sendCount < allRoutes.size());
}

void ReliableTransport::sendUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& inputRoutes)
{
    auto routes = iface.getTopController().filterAdvertisableRoutes(inputRoutes);

    auto versionedUpdate = [&](const TLVType& version)
    {
        if (iface.configs->isPassive.load(std::memory_order_relaxed) || iface.getNTable().size() == 0) return;
        auto& interface = iface.getIface();
        uint32_t bandwidthMetric = (10000000 / interface.configs.bandwidth.load(std::memory_order_relaxed));
        uint32_t delay = interface.configs.delay.load(std::memory_order_relaxed);
        bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
        size_t sent = 0;

        do
        {
            PacketBuilder eigrpPacket(&interface);
            af == AddressFamily::IPv4
                ? Protocol::IPPacket::reserveIpv4(&interface, eigrpPacket)
                : Protocol::IPPacket::reserveIpv6(&interface, eigrpPacket);

            uint32_t seqNum = incrementSequenceNumber();
            std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(eigrpPacket, Variable::Eigrp::Type::update, seqNum, 0, iface.getBase().getVirtualRouterID(), as);
            if (!eigrp.has_value())
            {
                iface.getIface().tx->release(eigrpPacket.frame);
                return;
            }

            // Add conditional receive if necessary
            if (!neighbor && ntable->size() > 1)
                eigrp->setFlagCondRecv(true);

            uint16_t mtuSize = getMtu();
            uint16_t maxRouteSize = mtuSize - static_cast<uint16_t>(eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));

            TLV16BufferManager opts(eigrp->getTrail().data(), maxRouteSize);

            EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
            
            const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(sent), routes.end());
            sent += EigrpPacketBuilder::appendRoutes(opts, availableRoutes, bandwidthMetric, delay, version);

            if (authentication)
            {
                opts.addLen(eigrpPacket.getMaxHeaderSize(mtuSize) - opts.size() - EigrpHeader::fixedSize);
                EigrpPacketBuilder::appendAuthTLV(opts, iface);
            }
            eigrpPacket.addTLVSize(opts.size());

            transmitReliable(eigrpPacket, neighbor, *eigrp);
        }
        while (sent < routes.size());
    };

    if (neighbor)
    {
        versionedUpdate(neighbor->tlvType);
    }
    else
    {
        for (auto& v : iface.tlvTypes)
        {
            versionedUpdate(v.first);
        }
    }
}

void ReliableTransport::sendQuery(Neighbor* neighbor, const std::vector<OutgoingQuery*>& queries)
{
    auto versionedQuery = [&](const TLVType& version)
    {
        if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

        auto& interface = iface.getIface();
        auto& base = iface.getBase();
        bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

        uint32_t bandwidthMetric = (10000000 / interface.configs.bandwidth.load(std::memory_order_relaxed));
        uint32_t delay = interface.configs.delay.load(std::memory_order_relaxed);

        size_t sent = 0;
        const uint16_t mtuSize = getMtu();

        do
        {
            PacketBuilder eigrpPacket(&interface);
            af == AddressFamily::IPv4
                ? Protocol::IPPacket::reserveIpv4(&interface, eigrpPacket)
                : Protocol::IPPacket::reserveIpv6(&interface, eigrpPacket);

            uint32_t seqNum = incrementSequenceNumber();
            std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
                eigrpPacket, Variable::Eigrp::Type::query, seqNum, 0, base.getVirtualRouterID(), as);
            if (!eigrp.has_value())
            {
                iface.getIface().tx->release(eigrpPacket.frame);
                return;
            }

            uint16_t maxSize = mtuSize - static_cast<uint16_t>(eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));
            TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

            EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());
            
            const std::vector<OutgoingQuery*> availableQueries = std::vector<OutgoingQuery*>(queries.begin() + static_cast<int>(sent), queries.end());
            sent += EigrpPacketBuilder::appendQueries(opts, availableQueries, seqNum, bandwidthMetric, delay, version);

            if (authentication)
            {
                opts.addLen(eigrpPacket.getMaxHeaderSize(mtuSize) - opts.size() - EigrpHeader::fixedSize);
                EigrpPacketBuilder::appendAuthTLV(opts, iface);
            }
            eigrpPacket.addTLVSize(opts.size());

            transmitReliable(eigrpPacket, neighbor, *eigrp);
        }
        while (sent < queries.size());
    };

    if (neighbor)
    {
        versionedQuery(neighbor->tlvType);
    }
    else
    {
        for (auto& v : iface.tlvTypes)
        {
            versionedQuery(v.first);
        }
    }
}

void ReliableTransport::sendReply(Neighbor& neighbor, const std::vector<const RouteInfo*>& routes, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto& interface = iface.getIface();
    auto& base = iface.getBase();
    bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    uint32_t bandwidthMetric = (10000000 / interface.configs.bandwidth.load(std::memory_order_relaxed));
    uint32_t delay = interface.configs.delay.load(std::memory_order_relaxed);

    uint32_t sent = 0;
    const uint16_t mtuSize = getMtu();

    do
    {
        PacketBuilder eigrpPacket(&interface);
        af == AddressFamily::IPv4
            ? Protocol::IPPacket::reserveIpv4(&interface, eigrpPacket)
            : Protocol::IPPacket::reserveIpv6(&interface, eigrpPacket);

        uint32_t seqNum = incrementSequenceNumber();
        std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
            eigrpPacket, Variable::Eigrp::Type::reply, seqNum, seq, base.getVirtualRouterID(), as); if (!eigrp.has_value())
        {
            interface.tx->release(eigrpPacket.frame);
            return;
        }

        uint16_t maxSize = mtuSize - static_cast<uint16_t>(eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));
        TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

        EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

        const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(sent), routes.end());
        sent += EigrpPacketBuilder::appendRoutes(opts, availableRoutes, bandwidthMetric, delay, neighbor.tlvType);

        if (authentication)
        {
            opts.addLen(eigrpPacket.getMaxHeaderSize(mtuSize) - opts.size() - EigrpHeader::fixedSize);
            EigrpPacketBuilder::appendAuthTLV(opts, iface);
        }

        eigrpPacket.addTLVSize(opts.size());
        transmitReliable(eigrpPacket, &neighbor, *eigrp);
    }
    while (sent < routes.size());
}

void ReliableTransport::sendSIAQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    
    auto& interface = iface.getIface();
    auto& base = iface.getBase();
    bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    uint32_t bandwidthMetric = (10000000 / interface.configs.bandwidth.load(std::memory_order_relaxed));
    uint32_t delay = interface.configs.delay.load(std::memory_order_relaxed);

    uint16_t mtuSize = getMtu();
    uint32_t sent = 0;

    do
    {
        PacketBuilder eigrpPacket(&interface);
        af == AddressFamily::IPv4
            ? Protocol::IPPacket::reserveIpv4(&interface, eigrpPacket)
            : Protocol::IPPacket::reserveIpv6(&interface, eigrpPacket);

        uint32_t seqNum = incrementSequenceNumber();
        std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
            eigrpPacket, Variable::Eigrp::Type::siaQuery, seqNum, 0, base.getVirtualRouterID(), as); if (!eigrp.has_value())
        {
            interface.tx->release(eigrpPacket.frame);
            return;
        }

        uint16_t maxSize = mtuSize - static_cast<uint16_t>(eigrpPacket.bufferOffset + EigrpHeader::fixedSize + (authentication ? 128 : 0));
        TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

        EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

        const std::vector<OutgoingQuery*> availableRoutes = std::vector<OutgoingQuery*>(routes.begin() + static_cast<int>(sent), routes.end());
        sent += EigrpPacketBuilder::appendSIAQueries(opts, availableRoutes, seqNum, bandwidthMetric, delay, neighbor.tlvType);

        if (authentication)
        {
            opts.addLen(eigrpPacket.getMaxHeaderSize(mtuSize) - opts.size() - EigrpHeader::fixedSize);
            EigrpPacketBuilder::appendAuthTLV(opts, iface);
        }

        eigrpPacket.addTLVSize(opts.size());
        transmitReliable(eigrpPacket, &neighbor, *eigrp);
    }
    while (sent < routes.size());
}

void ReliableTransport::sendSIAReply(Neighbor& neighbor, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto& interface = iface.getIface();
    auto& base = iface.getBase();
    bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    uint16_t mtuSize = getMtu();

    PacketBuilder eigrpPacket(&interface);
    af == AddressFamily::IPv4
        ? Protocol::IPPacket::reserveIpv4(&interface, eigrpPacket)
        : Protocol::IPPacket::reserveIpv6(&interface, eigrpPacket);

    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        eigrpPacket, Variable::Eigrp::Type::reply, seqNum, seq, base.getVirtualRouterID(), as); if (!eigrp.has_value())
    {
        interface.tx->release(eigrpPacket.frame);
        return;
    }

    uint16_t maxSize = static_cast<uint16_t>(eigrpPacket.getMaxHeaderSize(mtuSize));
    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    if (authentication)
    {
        opts.addLen(eigrpPacket.getMaxHeaderSize(mtuSize) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }

    eigrpPacket.addTLVSize(opts.size());
    transmitReliable(eigrpPacket, &neighbor, *eigrp);
}
}
