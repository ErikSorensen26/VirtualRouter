// ReliableTX.cpp

//TODO add int32 support for eigrp
#include "ReliableTransport.h"
#include "EigrpInterface.h"
#include "EigrpPacketBuilder.h"
#include <Eigrp.h>

#include <IPPacket.h>
#include <PacketBuilder.hpp>

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
        if (setupMulticastReliable(header))
        {
            transmit(pkt);
        }
    }
    else
    {
        if (setupUnicastReliable(*neighbor, header))
        {
            if (header.getSequence() == 0)
            {
                uint32_t ack = 0;
                if (neighbor->popAck(ack) && ack != 0)
                    header.setAck(ack);
            }
            transmit(pkt, neighbor->ipAddress.raw);
        }
    }
}

void ReliableTransport::releaseFailedPacket(PacketBuilder& builder)
{
    iface.getIface()->tx->release(builder.frame);
}

void ReliableTransport::createPacket(PacketBuilder& pkt)
{
    af == AddressFamily::IPv4
        ? Protocol::IPPacket::reserveIpv4(pkt)
        : Protocol::IPPacket::reserveIpv6(pkt);
}

void ReliableTransport::sendHello()
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    if (!createHello(pkt).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt);
}

void ReliableTransport::sendConditionalHello(const std::vector<IPAddress>& neighbors, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PktInfo info;
    info.mtu = getMtu();

    do
    {
        PacketBuilder pkt(iface.getIface());
        createPacket(pkt);
        if (auto eigrp = createConditionalHello(pkt, info, neighbors, seq); eigrp.has_value())
        {
            transmit(pkt);
        }
        else
            releaseFailedPacket(pkt);
    }
    while (info.sent < neighbors.size());
}

void ReliableTransport::sendUnicastHello(const IPAddress& neighborIp)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    if (!createUnicastHello(pkt).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, neighborIp.raw);
}

void ReliableTransport::trackAck(Neighbor& neighbor, uint32_t seq)
{
    neighbor.pushAck(seq);
}

void ReliableTransport::attemptSendAck(Neighbor& neighbor, uint32_t seq)
{
    if (neighbor.hasAck(seq))
        sendAck(neighbor, seq);
}

void ReliableTransport::sendAck(Neighbor& neighbor, uint32_t seq)
{
    neighbor.removeAck(seq);

    {
        std::lock_guard<std::mutex> lock(neighbor.reliableMtx);
        neighbor.receivedConditions.erase(seq);
    }

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);

    if (!createAck(pkt, seq).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, neighbor.ipAddress.raw);
}

void ReliableTransport::sendNullUpdate(Neighbor& neighbor)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    auto header = createNullUpdate(pkt);
    if (!header.has_value())
        return releaseFailedPacket(pkt);
    
    neighbor.sentInitSeq.store(header.value().getSequence());

    transmitReliable(pkt, &neighbor, header.value());
}

void ReliableTransport::sendFullTopology(Neighbor& neighbor, Resync resync)
{
    bool unicast = firstFullSend.exchange(true, std::memory_order_release);

    if (iface.configs->isPassive.load(std::memory_order_relaxed))
        return;
    if (neighbor.fullSent.exchange(true, std::memory_order_acq_rel))
        return; // Full top already sent

    auto* interface = iface.getIface();

    auto& topology = iface.getTopController();
    std::vector<const RouteInfo*> allRoutes = topology.filterAdvertisableRoutes(topology.getAllRoutes());
    bool empty = allRoutes.empty();
    if (empty)
        return;
    
    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.mtu = getMtu();

    auto versionedUpdate = [&](const TLVType& version)
    {
        info.version = version;
        bool first = true;

        do
        {
            PacketBuilder eigrpPacket(interface);
            createPacket(eigrpPacket);
            if (auto eigrp = createUpdate(eigrpPacket, info, &neighbor, allRoutes); eigrp.has_value())
            {
                if (resync != Resync::REPLY)
                    eigrp->setFlagInit(first);
                if (resync != Resync::NONE)
                    eigrp->setFlagRestart(first);
                if (info.sent >= allRoutes.size() && allRoutes.size() > 0)
                    eigrp->setFlagEndOfTable(true);

                transmitReliable(eigrpPacket, unicast ? &neighbor : nullptr, *eigrp);
                first = false;
            }
            else
                releaseFailedPacket(eigrpPacket);

        }
        while (info.sent < allRoutes.size());
    };

    for (auto& v : iface.tlvTypes)
    {
        versionedUpdate(v.first);
    }
}

void ReliableTransport::sendUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& inputRoutes)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed) || iface.getNTable().size() == 0) return;
    auto routes = iface.getTopController().filterAdvertisableRoutes(inputRoutes);
    if (routes.empty()) return;

    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.mtu = getMtu();

    auto versionedUpdate = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            PacketBuilder eigrpPacket(interface);
            createPacket(eigrpPacket);
            if (auto eigrp = createUpdate(eigrpPacket, info, neighbor, routes); eigrp.has_value())
                transmitReliable(eigrpPacket, neighbor, *eigrp);
            else
                releaseFailedPacket(eigrpPacket);
        }
        while (info.sent < routes.size());
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

void ReliableTransport::sendPoisenedUpdate(Neighbor* neighbor, const std::vector<const RouteInfo*>& inputRoutes)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed) || iface.getNTable().size() == 0) return;

    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = std::numeric_limits<uint64_t>::max();
    info.mtu = getMtu();

    auto versionedUpdate = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            PacketBuilder eigrpPacket(interface);
            createPacket(eigrpPacket);
            if (auto eigrp = createUpdate(eigrpPacket, info, neighbor, inputRoutes); eigrp.has_value())
                transmitReliable(eigrpPacket, neighbor, *eigrp);
            else
                releaseFailedPacket(eigrpPacket);
        }
        while (info.sent < inputRoutes.size());
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

void ReliableTransport::sendQuery(const std::vector<ActiveRoute*>& routes)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    
    auto versionedQuery = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            PacketBuilder eigrpPacket(interface);
            createPacket(eigrpPacket);
            if (auto eigrp = createQuery(eigrpPacket, info, routes); eigrp.has_value())
                transmitReliable(eigrpPacket, nullptr, *eigrp);
            else
                releaseFailedPacket(eigrpPacket);
        }
        while (info.sent < routes.size());
    };

    for (auto& v : iface.tlvTypes)
    {
        versionedQuery(v.first);
    }
}

void ReliableTransport::sendUnicastQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    
    auto versionedQuery = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            PacketBuilder eigrpPacket(interface);
            createPacket(eigrpPacket);
            if (auto eigrp = createUnicastQuery(eigrpPacket, info, neighbor, queries); eigrp.has_value())
                transmitReliable(eigrpPacket, &neighbor, *eigrp);
            else
                releaseFailedPacket(eigrpPacket);
        }
        while (info.sent < queries.size());
    };

    versionedQuery(neighbor.tlvType);
}

void ReliableTransport::sendReply(Neighbor& neighbor, const std::vector<const RouteInfo*>& replies)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.mtu = getMtu();

    do
    {
        PacketBuilder eigrpPacket(interface);
        createPacket(eigrpPacket);
        if (auto eigrp = createReply(eigrpPacket, info, neighbor, replies); eigrp.has_value())
            transmitReliable(eigrpPacket, &neighbor, *eigrp);
        else
            releaseFailedPacket(eigrpPacket);
    }
    while (info.sent < replies.size());
}

void ReliableTransport::sendSIAQuery(Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = interface->configs.bandwidth.load(std::memory_order_relaxed);
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.mtu = getMtu();

    do
    {
        PacketBuilder eigrpPacket(interface);
        createPacket(eigrpPacket);
        if (auto eigrp = createSIAQuery(eigrpPacket, info, queries); eigrp.has_value())
            transmitReliable(eigrpPacket, &neighbor, *eigrp);
        else
            releaseFailedPacket(eigrpPacket);
    }
    while (info.sent < queries.size());
}

void ReliableTransport::sendSIAReply(Neighbor& neighbor)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto* interface = iface.getIface();
    PacketBuilder eigrpPacket(interface);
    createPacket(eigrpPacket);
    if (auto eigrp = createSIAReply(eigrpPacket); eigrp.has_value())
        transmitReliable(eigrpPacket, &neighbor, *eigrp);
    else
        releaseFailedPacket(eigrpPacket);
}

std::optional<EigrpHeader> ReliableTransport::createHello(PacketBuilder& builder)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createConditionalHello(PacketBuilder& builder, PktInfo& info, const std::vector<IPAddress>& neighbors, uint32_t seq)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize);
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), maxSize);
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);

    const std::vector<IPAddress> availableNeighbors = std::vector<IPAddress>(neighbors.begin() + static_cast<int>(info.sent), neighbors.end());
    info.sent += EigrpPacketBuilder::appendSequenceTLVs(opts, neighbors);
    
    EigrpPacketBuilder::appendMulticastSeqTLV(opts, seq);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createUnicastHello(PacketBuilder& builder)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createAck(PacketBuilder& builder, uint32_t seq)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::hello,
        0, seq,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    builder.addTLVSize(0);
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createNullUpdate(PacketBuilder& builder)
{
    auto& base = iface.getBase();
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::update,
        incrementSequenceNumber(),
        0,
        base.getVirtualRouterID(),
        base.getAS()
    );
    if (!eigrpHeader) return std::nullopt;
    eigrpHeader->setFlagInit(true);

    uint16_t mtuSize = getMtu();
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(mtuSize));
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    // restart?
    builder.addTLVSize(opts.size());
    eigrpHeader.value().setTrailSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createUpdate(PacketBuilder& builder, PktInfo& info, Neighbor* neighbor, const std::vector<const RouteInfo*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(builder, Variable::Eigrp::Type::update, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize);

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(info.sent), routes.end());
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableRoutes, info.bandwidthMetric, info.delay, info.version);

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createQuery(PacketBuilder& builder, PktInfo& info, const std::vector<ActiveRoute*>& queries)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::query, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize);

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());
    
    size_t initSize = info.sent;
    std::vector<const RouteInfo*> availableQueries;
    availableQueries.reserve(queries.size() - info.sent);
    for (auto it = queries.begin() + initSize; it != queries.end(); it++)
        availableQueries.push_back((*it)->originRoute);
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableQueries, info.bandwidthMetric, std::numeric_limits<uint64_t>::max(), info.version);

    for (auto it = queries.begin() + initSize; it != queries.begin() + info.sent; it++)
        for (auto& n : (*it)->pendingQueries)
            if (n.second.querySequence == 0)
                n.second.querySequence = seqNum;

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createUnicastQuery(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::query, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize);

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());
    
    size_t initSize = info.sent;
    std::vector<const RouteInfo*> availableQueries;
    availableQueries.reserve(queries.size() - info.sent);
    for (auto it = queries.begin() + initSize; it != queries.end(); it++)
        availableQueries.push_back((*it)->route->originRoute);
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableQueries, info.bandwidthMetric, info.delay, info.version);

    for (auto it = queries.begin() + initSize; it != queries.begin() + info.sent; it++)
        (*it)->querySequence = seqNum;

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createReply(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<const RouteInfo*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::reply, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize);
    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(info.sent), routes.end());
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableRoutes, info.bandwidthMetric, info.delay, neighbor.tlvType);

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createSIAQuery(PacketBuilder& builder, PktInfo& info, const std::vector<OutgoingQuery*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::siaQuery, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize);

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    size_t initSize = info.sent;
    std::vector<const RouteInfo*> availableQueries;
    availableQueries.reserve(routes.size() - info.sent);
    for (auto it = routes.begin() + initSize; it != routes.end(); it++)
        availableQueries.push_back((*it)->route->originRoute);
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableQueries, info.bandwidthMetric, info.delay, info.version);

    for (auto it = routes.begin() + initSize; it != routes.begin() + info.sent; it++)
        (*it)->siaSequence = seqNum;

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createSIAReply(PacketBuilder& builder)
{
    auto& base = iface.getBase();

    uint16_t mtu = getMtu();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::reply, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = static_cast<uint16_t>(builder.getMaxHeaderSize(mtu));
    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}
}
