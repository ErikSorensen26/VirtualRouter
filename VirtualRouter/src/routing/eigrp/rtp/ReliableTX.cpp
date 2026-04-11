// ReliableTX.cpp

#include "hardware/HardwareManager.h"
#include "ReliableTransport.h"
#include "eigrp/interface/EigrpInterface.h"
#include "EigrpPacketBuilder.h"
#include "eigrp/core/Eigrp.h"

#include "infrastructure/IPPacket.h"
#include "processing/PacketBuilder.hpp"

namespace routing::eigrp
{
void ReliableTransport::transmit(processing::PacketBuilder& pkt, const types::IPAddress* dest)
{
    types::IPAddress target;
    if (dest) {
        target = *dest;
    } else if (af == types::AddressFamily::IPv4) {
        target = types::IPAddress(EIGRP_MULTICAST_ADDRESS, types::AddressFamily::IPv4);
    } else {
        target = types::IPAddress(EIGRP_MULTICAST_ADDRESS_V6, types::AddressFamily::IPv6);
    }

    auto* interface = iface.getIface();
    infrastructure::ippacket::BuildIP build = {
        .iface = interface,
        .packetInfo = pkt,
        .destIp = target,
        .DSCP = iface.DSCP.load(std::memory_order_relaxed),
        .protocolType = IP_EIGRP
    };

    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::buildIpv4(build)
        : infrastructure::ippacket::buildIpv6(build);
}

void ReliableTransport::transmitReliable(processing::PacketBuilder& pkt, Neighbor* neighbor, packet::EigrpHeader& header)
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
            transmit(pkt, &neighbor->ipAddress);
        }
    }
}

void ReliableTransport::releaseFailedPacket(processing::PacketBuilder& builder)
{
    iface.getIface()->tx->release(builder.frame);
}

void ReliableTransport::createPacket(processing::PacketBuilder& pkt)
{
    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::reserveIpv4(pkt)
        : infrastructure::ippacket::reserveIpv6(pkt);
}

void ReliableTransport::sendHello()
{
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;

    processing::PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    if (!createHello(pkt).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt);
}

void ReliableTransport::sendConditionalHello(const std::vector<types::IPAddress>& neighbors, uint32_t seq)
{
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;

    PktInfo info;
    info.mtu = getMtu();

    do
    {
        processing::PacketBuilder pkt(iface.getIface());
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

void ReliableTransport::sendUnicastHello(const types::IPAddress& neighborIp)
{
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;

    processing::PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    if (!createUnicastHello(pkt).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, &neighborIp);
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
    neighbor.receivedConditions.erase(seq);

    processing::PacketBuilder pkt(iface.getIface());
    createPacket(pkt);

    if (!createAck(pkt, seq).has_value())
        return releaseFailedPacket(pkt);

    transmit(pkt, &neighbor.ipAddress);
}

void ReliableTransport::sendNullUpdate(Neighbor& neighbor)
{
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;

    processing::PacketBuilder pkt(iface.getIface());
    createPacket(pkt);
    auto header = createNullUpdate(pkt);
    if (!header.has_value())
        return releaseFailedPacket(pkt);
    
    neighbor.sentInitSeq.store(header.value().getSequence());

    transmitReliable(pkt, &neighbor, header.value());
}

void ReliableTransport::sendFullTopology(Neighbor& neighbor, Resync resync)
{
    bool unicast = !iface.multicastEnabledFlag.load(std::memory_order_relaxed)
               || firstFullSend.exchange(true, std::memory_order_release);

    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load())
        return;
    if (neighbor.fullSent.exchange(true, std::memory_order_acq_rel))
        return; // Full top already sent

    auto* interface = iface.getIface();

    auto& topology = iface.getTopController();
    std::vector<const RouteInfo*> allRoutes = topology.getAdvertisableRoutes();
    if (allRoutes.empty())
        return;
    
    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = 0;
    info.mtu = getMtu();

    auto versionedUpdate = [&](const TLVType& version)
    {
        info.version = version;
        bool first = true;

        do
        {
            processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load() || iface.getNTable().size() == 0) return;
    auto routes = iface.getTopController().filterAdvertisableRoutes(inputRoutes);
    if (routes.empty()) return;

    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = 0;
    info.mtu = getMtu();

    auto versionedUpdate = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load() || iface.getNTable().size() == 0) return;

    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = std::numeric_limits<uint64_t>::max();
    info.mtu = getMtu();

    auto versionedUpdate = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = 0;
    
    auto versionedQuery = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = 0;
    
    auto versionedQuery = [&](const TLVType& version)
    {
        info.version = version;

        do
        {
            processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = 0;
    info.mtu = getMtu();

    do
    {
        processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;
    auto* interface = iface.getIface();

    PktInfo info;
    info.bandwidthMetric = static_cast<uint32_t>(interface->configs.hwInfo.bandwidth / 1000);
    info.delay = 0;
    info.mtu = getMtu();

    do
    {
        processing::PacketBuilder eigrpPacket(interface);
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
    if (iface.configs.get<config::EigrpInterface::PASSIVE_INTERFACE>().load()) return;

    auto* interface = iface.getIface();
    processing::PacketBuilder eigrpPacket(interface);
    createPacket(eigrpPacket);
    if (auto eigrp = createSIAReply(eigrpPacket); eigrp.has_value())
        transmitReliable(eigrpPacket, &neighbor, *eigrp);
    else
        releaseFailedPacket(eigrpPacket);
}

std::optional<packet::EigrpHeader> ReliableTransport::createHello(processing::PacketBuilder& builder)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        EIGRP_TYPE_HELLO,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    
    packet::TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<packet::EigrpHeader> ReliableTransport::createConditionalHello(processing::PacketBuilder& builder, PktInfo& info, const std::vector<types::IPAddress>& neighbors, uint32_t seq)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        EIGRP_TYPE_HELLO,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + packet::EigrpHeader::fixedSize);
    
    packet::TLV16BufferManager opts(eigrpHeader->getTrail().data(), maxSize);
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);

    const std::vector<types::IPAddress> availableNeighbors(neighbors.begin() + static_cast<int>(info.sent), neighbors.end());
    info.sent += EigrpPacketBuilder::appendSequenceTLVs(opts, availableNeighbors);
    
    EigrpPacketBuilder::appendMulticastSeqTLV(opts, seq);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<packet::EigrpHeader> ReliableTransport::createUnicastHello(processing::PacketBuilder& builder)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        EIGRP_TYPE_HELLO,
        0, 0,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    
    packet::TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(getMtu()));
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendParameterTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    EigrpPacketBuilder::appendVersionTLV(opts);
    builder.addTLVSize(opts.size());
    return eigrpHeader;
}

std::optional<packet::EigrpHeader> ReliableTransport::createAck(processing::PacketBuilder& builder, uint32_t seq)
{
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        EIGRP_TYPE_HELLO,
        0, seq,
        iface.getBase().getVirtualRouterID(),
        as
    );
    if (!eigrpHeader) return std::nullopt;
    builder.addTLVSize(0);
    return eigrpHeader;
}

std::optional<packet::EigrpHeader> ReliableTransport::createNullUpdate(processing::PacketBuilder& builder)
{
    auto& base = iface.getBase();
    auto eigrpHeader = EigrpPacketBuilder::buildHeader(builder,
        EIGRP_TYPE_UPDATE,
        incrementSequenceNumber(),
        0,
        base.getVirtualRouterID(),
        base.getAS()
    );
    if (!eigrpHeader) return std::nullopt;
    eigrpHeader->setFlagInit(true);

    uint16_t mtuSize = getMtu();
    packet::TLV16BufferManager opts(eigrpHeader->getTrail().data(), builder.getMaxHeaderSize(mtuSize));
    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, iface.getBase().getGlobalConfigMgr());
    // restart?
    builder.addTLVSize(opts.size());
    eigrpHeader.value().setTrailSize(opts.size());
    return eigrpHeader;
}

std::optional<packet::EigrpHeader> ReliableTransport::createUpdate(processing::PacketBuilder& builder, PktInfo& info, Neighbor* neighbor, const std::vector<const RouteInfo*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<packet::EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(builder, EIGRP_TYPE_UPDATE, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + packet::EigrpHeader::fixedSize);

    packet::TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(info.sent), routes.end());
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableRoutes, info.bandwidthMetric, info.delay, info.version);

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<packet::EigrpHeader> ReliableTransport::createQuery(processing::PacketBuilder& builder, PktInfo& info, const std::vector<ActiveRoute*>& queries)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<packet::EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
        builder, EIGRP_TYPE_QUERY, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + packet::EigrpHeader::fixedSize);

    packet::TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

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

std::optional<packet::EigrpHeader> ReliableTransport::createUnicastQuery(processing::PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<OutgoingQuery*>& queries)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<packet::EigrpHeader> eigrp = EigrpPacketBuilder::buildHeader(
        builder, EIGRP_TYPE_QUERY, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + packet::EigrpHeader::fixedSize);

    packet::TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

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

std::optional<packet::EigrpHeader> ReliableTransport::createReply(processing::PacketBuilder& builder, PktInfo& info, Neighbor& neighbor, const std::vector<const RouteInfo*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<packet::EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, EIGRP_TYPE_REPLY, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + packet::EigrpHeader::fixedSize);
    packet::TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    const std::vector<const RouteInfo*> availableRoutes = std::vector<const RouteInfo*>(routes.begin() + static_cast<int>(info.sent), routes.end());
    info.sent += EigrpPacketBuilder::appendRoutes(iface, opts, availableRoutes, info.bandwidthMetric, info.delay, neighbor.tlvType);

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}

std::optional<packet::EigrpHeader> ReliableTransport::createSIAQuery(processing::PacketBuilder& builder, PktInfo& info, const std::vector<OutgoingQuery*>& routes)
{
    auto& base = iface.getBase();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<packet::EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, EIGRP_TYPE_SIA_QUERY, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = info.mtu - static_cast<uint16_t>(builder.bufferOffset + packet::EigrpHeader::fixedSize);

    packet::TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

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

std::optional<packet::EigrpHeader> ReliableTransport::createSIAReply(processing::PacketBuilder& builder)
{
    auto& base = iface.getBase();

    uint16_t mtu = getMtu();
    uint32_t seqNum = incrementSequenceNumber();
    std::optional<packet::EigrpHeader> eigrp =  EigrpPacketBuilder::buildHeader(
        builder, EIGRP_TYPE_SIA_REPLY, seqNum, 0, base.getVirtualRouterID(), as);
    if (!eigrp.has_value()) return std::nullopt;

    uint16_t maxSize = static_cast<uint16_t>(builder.getMaxHeaderSize(mtu));
    packet::TLV16BufferManager opts(eigrp->getTrail().data(), maxSize);

    EigrpPacketBuilder::appendAuthTLV(opts, iface);
    EigrpPacketBuilder::appendStubTLV(opts, base.getGlobalConfigMgr());

    builder.addTLVSize(opts.size());
    eigrp.value().setTrailSize(opts.size());
    return eigrp;
}
} // namespace routing
