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
        std::shared_lock<std::shared_mutex> lock(ntable->neighborMutex);
        for (auto& [ip, nbr] : ntable->neighbors)
        {
            setupReliablePacket(&nbr, header);
        }
        transmit(pkt);
    }
    else
    {
        if (setupReliablePacket(neighbor, header))
            transmit(pkt, neighbor->ipAddress.raw);
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

void ReliableTransport::sendSequenceHello(const IPAddress& neighborIp, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    if (!createSequenceHello(pkt, neighborIp, seq).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, neighborIp.raw);
}

void ReliableTransport::sendUnicastHello(const IPAddress& neighborIp)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    if (!createUnicastHello(pkt, neighborIp).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, neighborIp.raw);
}

void ReliableTransport::sendAck(Neighbor& neighbor, uint32_t seq)
{
    if (neighbor.getState() != Neighbor::State::LOADING)
    {
        neighbor.pushAck(seq);
        return;
    }

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);

    if (!createAck(pkt, neighbor, seq).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, neighbor.ipAddress.raw);
}

void ReliableTransport::sendCondAck(Neighbor& neighbor, uint32_t seq)
{
    iface.getTimers().scheduleConditionalReceive(neighbor, seq);
}

void ReliableTransport::sendNullUpdate(Neighbor& neighbor)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    auto header = createNullUpdate(pkt, neighbor);
        return releaseFailedPacket(pkt);

    transmitReliable(pkt, &neighbor, header.value());
}

void ReliableTransport::sendFullTopology(Neighbor& neighbor)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto* interface = iface.getIface();

    auto topology = iface.getTopController();
    std::vector<const RouteInfo*> allRoutes = topology.filterAdvertisableRoutes(topology.getAllRoutes());
    if (allRoutes.empty()) return;
    
    PktInfo info;
    info.bandwidthMetric = (10000000 / interface->configs.bandwidth.load(std::memory_order_relaxed));
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
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
                eigrp->setFlagInit(first);
                if (info.sent >= allRoutes.size() && allRoutes.size() > 0)
                    eigrp->setFlagEndOfTable(true);

                transmitReliable(eigrpPacket, &neighbor, *eigrp);
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
    info.bandwidthMetric = (10000000 / interface->configs.bandwidth.load(std::memory_order_relaxed));
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
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

void ReliableTransport::sendQuery(const std::vector<ActiveRoute*>& routes)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = (10000000 / interface->configs.bandwidth.load(std::memory_order_relaxed));
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
    
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
    info.bandwidthMetric = (10000000 / interface->configs.bandwidth.load(std::memory_order_relaxed));
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);
    
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

void ReliableTransport::sendReply(Neighbor& neighbor, const std::vector<const RouteInfo*>& replies, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = (10000000 / interface->configs.bandwidth.load(std::memory_order_relaxed));
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    do
    {
        PacketBuilder eigrpPacket(interface);
        createPacket(eigrpPacket);
        if (auto eigrp = createReply(eigrpPacket, info, neighbor, replies, seq); eigrp.has_value())
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
    info.bandwidthMetric = (10000000 / interface->configs.bandwidth.load(std::memory_order_relaxed));
    info.delay = interface->configs.delay.load(std::memory_order_relaxed);
    info.authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    do
    {
        PacketBuilder eigrpPacket(interface);
        createPacket(eigrpPacket);
        if (auto eigrp = createSIAQuery(eigrpPacket, info, neighbor, queries); eigrp.has_value())
            transmitReliable(eigrpPacket, &neighbor, *eigrp);
        else
            releaseFailedPacket(eigrpPacket);
    }
    while (info.sent < queries.size());
}

void ReliableTransport::sendSIAReply(Neighbor& neighbor, uint32_t seq)
{
    if (iface.configs->isPassive.load(std::memory_order_relaxed)) return;

    auto* interface = iface.getIface();
    PacketBuilder eigrpPacket(interface);
    createPacket(eigrpPacket);
    if (auto eigrp = createSIAReply(eigrpPacket, neighbor, seq); eigrp.has_value())
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
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createSequenceHello(PacketBuilder& builder, const IPAddress& neighborIp, uint32_t seq)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendSequenceTLV(opts, neighborIp);
    EigrpPacketBuilder::appendMulticastSeqTLV(opts, seq);
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createUnicastHello(PacketBuilder& builder, const IPAddress& neighborIp)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        Variable::Eigrp::Type::hello,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    
    TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createAck(PacketBuilder& builder, Neighbor& neighbor, uint32_t seq)
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

std::optional<EigrpHeader> ReliableTransport::createNullUpdate(PacketBuilder& builder, Neighbor& neighbor)
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
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    // restart?
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<EigrpHeader> ReliableTransport::createUpdate(PacketBuilder& builder, PktInfo& info, Neighbor* neighbor, const std::vector<const RouteInfo*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(builder, Variable::Eigrp::Type::update, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    // Add conditional receive if necessary
    if (!neighbor && ntable->size() > 1)
        eigrp->setFlagCondRecv(true);

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize + (info.authentication ? 128 : 0));

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(info.sent), routes.end());
    info.sent += EigrpPacketBuilder::appendRoutes(opts, availableRoutes, info.bandwidthMetric, info.delay, info.version);

    if (info.authentication)
    {
        opts.addLen(builder.getMaxHeaderSize(info.mtu) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }
    builder.addTLVSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createQuery(PacketBuilder& builder, PktInfo& info, const std::vector<ActiveRoute*>& queries)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::query, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    // Add conditional receive if necessary
    if (ntable->size() > 1)
        eigrp->setFlagCondRecv(true);

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize + (info.authentication ? 128 : 0));

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());
    
    size_t initSize = info.sent;
    std::vector<const RouteInfo*> availableQueries;
    availableQueries.reserve(queries.size() - info.sent);
    for (auto it = queries.begin() + initSize; it != queries.end(); it++)
        availableQueries.push_back((*it)->originRoute);
    info.sent += EigrpPacketBuilder::appendRoutes(opts, availableQueries, info.bandwidthMetric, info.delay, info.version);

    for (auto it = queries.begin() + initSize; it != queries.begin() + info.sent; it++)
        for (auto& n : (*it)->pendingQueries)
            if (n.second.querySequence == 0)
                n.second.querySequence = seqNum;

    if (info.authentication)
    {
        opts.addLen(builder.getMaxHeaderSize(info.mtu) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }
    builder.addTLVSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createUnicastQuery(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::query, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize + (info.authentication ? 128 : 0));

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());
    
    size_t initSize = info.sent;
    std::vector<const RouteInfo*> availableQueries;
    availableQueries.reserve(queries.size() - info.sent);
    for (auto it = queries.begin() + initSize; it != queries.end(); it++)
        availableQueries.push_back((*it)->route->originRoute);
    info.sent += EigrpPacketBuilder::appendRoutes(opts, availableQueries, info.bandwidthMetric, info.delay, info.version);

    for (auto it = queries.begin() + initSize; it != queries.begin() + info.sent; it++)
        (*it)->querySequence = seqNum;

    if (info.authentication)
    {
        opts.addLen(builder.getMaxHeaderSize(info.mtu) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }
    builder.addTLVSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createReply(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<const RouteInfo*>& routes, uint32_t seq)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::reply, seqNum, seq, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize + (info.authentication ? 128 : 0));
    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(info.sent), routes.end());
    info.sent += EigrpPacketBuilder::appendRoutes(opts, availableRoutes, info.bandwidthMetric, info.delay, neighbor.tlvType);

    if (info.authentication)
    {
        opts.addLen(builder.getMaxHeaderSize(info.mtu) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }

    builder.addTLVSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createSIAQuery(PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::siaQuery, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + EigrpHeader::fixedSize + (info.authentication ? 128 : 0));

    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    size_t initSize = info.sent;
    std::vector<const RouteInfo*> availableQueries;
    availableQueries.reserve(routes.size() - info.sent);
    for (auto it = routes.begin() + initSize; it != routes.end(); it++)
        availableQueries.push_back((*it)->route->originRoute);
    info.sent += EigrpPacketBuilder::appendRoutes(opts, availableQueries, info.bandwidthMetric, info.delay, info.version);

    for (auto it = routes.begin() + initSize; it != routes.begin() + info.sent; it++)
        (*it)->siaSequence = seqNum;

    if (info.authentication)
    {
        opts.addLen(builder.getMaxHeaderSize(info.mtu) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }

    builder.addTLVSize(opts.size());
    return eigrp;
}

std::optional<EigrpHeader> ReliableTransport::createSIAReply(PacketBuilder& builder, Neighbor& neighbor, uint32_t seq)
{
    auto& base = iface.getBase();
    bool authentication = iface.configs->authKey.fullyEnabled.load(std::memory_order_relaxed);

    uint16_t mtu = getMtu();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, Variable::Eigrp::Type::reply, seqNum, seq, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = static_cast<uint16_t>(builder.getMaxHeaderSize(mtu));
    TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    if (authentication)
    {
        opts.addLen(builder.getMaxHeaderSize(mtu) - opts.size() - EigrpHeader::fixedSize);
        EigrpPacketBuilder::appendAuthTLV(opts, iface);
    }

    builder.addTLVSize(opts.size());
    return eigrp;
}
}
