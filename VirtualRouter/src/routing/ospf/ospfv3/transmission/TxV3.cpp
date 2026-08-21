// Ospfv3Tx.cpp

// NOTE: set ttl to 1

#include "PacketDispatcherV3.h"
#include "processing/PacketBuilder.hpp"
#include "infrastructure/IPPacket.h"
#include "ospf/OspfProcess.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/transmission/OspfPacket.hpp"

#include "ospf/database/LsdbTypes.hpp"
#include "ospf/area/Area.h"
#include "ospf/FlagManager.hpp"

#include "packet/headers/embedded/ospf/Ospfv3HelloHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3DBDHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3LSAHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3LSRHeader.hpp"

namespace routing::ospf
{
uint16_t PacketDispatcherV3::getMtu()
{
    return iface.getTransmitInterface()->configs.ipv4.mtu.load(std::memory_order_relaxed);
}

void PacketDispatcherV3::finalizeHeader(packet::Ospfv3Header& hdr, OspfBuilder& builder, bool lls, Neighbor* nbr)
{
    hdr.setPacketLen(static_cast<uint16_t>(builder.offset + packet::Ospfv3Header::fixedSize));

    if (lls)
    {
        bool restart = iface.getGracefulRestartInProgress();
        bool resync = nbr && nbr->resyncRequested.exchange(false, std::memory_order_relaxed);

        uint8_t* buf = builder.getBuf();
        uint16_t size = addLinkLocalExtension(buf, restart, resync);
        addLinkLocalChecksum(buf);
        builder.offset += size;
    }

    hdr.setTrailSize(builder.offset);
    builder.pkt.addTLVSize(builder.offset);
}

void PacketDispatcherV3::sendHello()
{
    processing::PacketBuilder pkt(iface.getTransmitInterface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    auto lls = iface.getLls();
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt);
}

void PacketDispatcherV3::sendUnicastHello(Neighbor& nbr)
{
    processing::PacketBuilder pkt(iface.getTransmitInterface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.getLls();
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls, &nbr);
    transmit(pkt, &nbr.ipAddress);
}

void PacketDispatcherV3::sendInitDbd(Neighbor& nbr)
{
    processing::PacketBuilder pkt(iface.getTransmitInterface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_DATABASE_DESCRIPTION);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.getLls();
    auto dbd = buildDBD(builder, nbr, lls);
    if (!dbd.has_value()) return;

    dbd->setFlagI(true);
    dbd->setFlagM(true);
    dbd->setFlagMS(true);

    builder.offset = packet::Ospfv3DBDHeader::fixedSize;
    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, &nbr.ipAddress);
}

bool PacketDispatcherV3::sendDbd(Neighbor& nbr)
{
    processing::PacketBuilder pkt(iface.getTransmitInterface());

    if (nbr.getRole() == Neighbor::Role::MASTER)
        nbr.currentSeq.fetch_add(1); // Add sequence if MASTER, otherwise ACK previous sequence.

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_DATABASE_DESCRIPTION);
    if (!ospfHeader) return false;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.getLls();
    auto db = buildDBD(builder, nbr, lls);
    if (!db.has_value()) return false;

    buildDescriptions(builder, nbr);
    if (nbr.currentDbd)
        db->setFlagM(true); // More to process

    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, &nbr.ipAddress);

    return true;
}

bool PacketDispatcherV3::sendLsAck(Neighbor& nbr, std::vector<LsaRecordRef>& acks)
{
    std::deque<processing::PacketBuilder> pkts;

    size_t sent = 0;
    while (sent < acks.size())
    {
        processing::PacketBuilder& pkt = pkts.emplace_back(iface.getTransmitInterface());

        auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_LINK_STATE_ACK);
        if (!ospfHeader) return false;

        uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
        uint8_t* trail = ospfHeader->getTrailData();

        OspfBuilder builder{pkt, trail, 0, maxSize};

        using AckList = std::span<LsaRecordRef>;
        AckList ackList = AckList(acks.data() + sent, acks.size() - sent);
        size_t acksSent = addLSAcks(builder, ackList);
        if (acksSent == 0) return false;
        sent += acksSent;

        finalizeHeader(*ospfHeader, builder);
    }

    for (auto& pkt : pkts)
        transmit(pkt, &nbr.ipAddress);

    return true;
}

bool PacketDispatcherV3::sendLsr(Neighbor& nbr)
{
    auto& lsrs = nbr.getRtr().lsrs();
    if (!lsrs.burstActive()) lsrs.beginRetransmitBurst();

    auto request = buildLSRequest(nbr);
    if (!request.has_value()) return false;
    transmit(request.value(), &nbr.ipAddress);

    return true;
}

bool PacketDispatcherV3::sendLsu(Neighbor* nbr)
{
    auto& lsus = nbr ? nbr->getRtr().lsus() : multicastLsus;
    if (!lsus.burstActive()) lsus.beginRetransmitBurst();

    std::vector<LsaRecordRef> sent;
    auto pkt = buildLSUpdate(nbr);
    if (!pkt.has_value()) return false;

    if (nbr)
    {
        transmit(pkt.value(), &nbr->ipAddress);
    }
    else
    {
        if (static_cast<OspfInterface*>(&iface)->getIsDr())
        {
            transmit(pkt.value());
        }
        else
        {
            static const types::IPAddress allDRouters(OSPFV3_ALL_D_ROUTERS, types::AddressFamily::IPv6);
            transmit(pkt.value(), &allDRouters);
        }
    }

    return true;
}

std::optional<processing::PacketBuilder> PacketDispatcherV3::buildLSRequest(Neighbor& nbr)
{
    processing::PacketBuilder pkt(iface.getTransmitInterface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_LINK_STATE_REQUEST);
    if (!ospfHeader)
        return std::nullopt;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    size_t sent = addLSRequests(builder, nbr);
    if (sent == 0) 
        return std::nullopt;

    finalizeHeader(*ospfHeader, builder);

    return pkt;
}

std::optional<processing::PacketBuilder> PacketDispatcherV3::buildLSUpdate(Neighbor* nbr)
{
    processing::PacketBuilder pkt(iface.getTransmitInterface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_LINK_STATE_UPDATE);
    if (!ospfHeader)
        return std::nullopt;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    // Reserve 2 bytes for LSA count
    OspfBuilder builder{pkt, trail, 0, maxSize};
    builder.offset += 2;

    auto updSent = addLSUpdates(builder, nbr);
    utils::write<uint16_t>(trail, static_cast<uint16_t>(updSent));
    if (updSent == 0)
        return std::nullopt;

    finalizeHeader(*ospfHeader, builder);

    return pkt;
}

std::optional<packet::Ospfv3Header> PacketDispatcherV3::buildHeader(processing::PacketBuilder& pkt, uint8_t type)
{
    af == types::AddressFamily::IPv4
        ? infrastructure::ippacket::reserveIpv4(pkt)
        : infrastructure::ippacket::reserveIpv6(pkt);

    packet::Ospfv3Header ospf = pkt.reserveAndBuildHeader<packet::Ospfv3Header>(packet::HeaderType::OSPFV3);
    if (!ospf.buffer) return std::nullopt;

    ospf.setVersion(OSPFV3_VERSION);
    ospf.setType(type);
    ospf.setRouterID(iface.area.process.getRouterId());
    ospf.setAreaID(iface.getAreaId());

    return ospf;
}

std::optional<packet::Ospfv3HelloHeader> PacketDispatcherV3::buildHello(OspfBuilder& builder, bool lls)
{
    if (!builder.hasRoom(packet::Ospfv3HelloHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv3HelloHeader hello;
    hello.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv3HelloHeader::fixedSize;

    hello.setInterfaceID(iface.interfaceId);
    hello.setHelloInterval(getHelloInterval());

    uint32_t options = getIfaceFlags();
    if (lls) AreaFlagManager::setLBit(options, true);
    hello.setOptions(options);

    hello.setRouterPriority(iface.getPriority());
    hello.setDeadInterval(getDeadInterval());
    auto ntype = iface.getNetworkType();
    if (ntype != config::ospf::NetworkType::POINT_TO_POINT)
    {
        hello.setDrID(static_cast<OspfInterface*>(&iface)->getDrRid());
        hello.setBdrID(static_cast<OspfInterface*>(&iface)->getBdrRid());
    }
    else
    {
        hello.setDrID(0);
        hello.setBdrID(0);
    }

    if (ntype == config::ospf::NetworkType::BROADCAST || ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        auto result = ntable.addNeighborList(builder.getBuf(), builder.maxSize - builder.offset);
        if (!result.has_value()) return std::nullopt;
        builder.offset += result.value();
    }

    return hello;
}

std::optional<packet::Ospfv3DBDHeader> PacketDispatcherV3::buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls)
{
    if (!builder.hasRoom(packet::Ospfv3DBDHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv3DBDHeader dbd;
    dbd.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv3DBDHeader::fixedSize;

    uint32_t options = getIfaceFlags();
    if (lls) AreaFlagManager::setLBit(options, true);
    dbd.setOptions(options);

    dbd.setMtu(iface.getTransmitInterface()->configs.ipv6.mtu.load(std::memory_order_relaxed));
    dbd.setSequence(nbr.currentSeq.load(std::memory_order_relaxed));

    return dbd;
}

std::optional<packet::Ospfv3LSAHeader> PacketDispatcherV3::buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction)
{
    if (!builder.hasRoom(packet::Ospfv3LSAHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv3LSAHeader db;
    db.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv3LSAHeader::fixedSize;

    // Check if self originated
    db.setAge(calculateAge(floodReduction, record));
    db.setType(static_cast<uint8_t>(key.lsaType));
    db.setLsID(key.linkStateId);
    db.setAdvRouter(key.advertisingRouter);
    db.setSeqNum(record.header.sequence);

    auto calcs = runLsaCalculations<PolicyV3>(record.header, key, record.body);
     
    db.setChecksum(calcs.checksum);
    db.setLen(calcs.size);

    return db;
}

std::optional<packet::Ospfv3LSAHeader> PacketDispatcherV3::buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record)
{
    if (!builder.hasRoom(packet::Ospfv3LSAHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv3LSAHeader db;
    db.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv3LSAHeader::fixedSize;

    db.setAge(record.header.age);
    db.setType(static_cast<uint8_t>(key.lsaType));
    db.setLsID(key.linkStateId);
    db.setAdvRouter(key.advertisingRouter);
    db.setSeqNum(record.header.sequence);
    db.setChecksum(record.header.checksum);
    db.setLen(record.header.length);

    return db;
}

size_t PacketDispatcherV3::addLSRequests(OspfBuilder& builder, Neighbor& nbr)
{
    size_t sent{0};
    auto& list = nbr.getRtr().lsrs();

    while (true)
    {
        if (!builder.hasRoom(packet::Ospfv3LSRHeader::fixedSize))
            break;

        LsaKey key;
        if (!list.nextInBurst(key))
            break;

        packet::Ospfv3LSRHeader lsr;
        lsr.setBuffer(builder.getBuf());
        lsr.setType(key.lsaType);
        lsr.setLsID(key.linkStateId);
        lsr.setAdvRouter(key.advertisingRouter);
        builder.offset += packet::Ospfv3LSRHeader::fixedSize;
        list.markBurst(key);
        sent++;
    }
    return sent;
}

size_t PacketDispatcherV3::addLSAcks(OspfBuilder& builder, std::span<LsaRecordRef>& acks)
{
    size_t sent{0};
    for (auto& ack : acks)
    {
        auto lsa = buildCopyLSAHeader(builder, ack.key, *ack.record);
        if (!lsa.has_value()) return sent;
        sent++;
    }
    return sent;
}

size_t PacketDispatcherV3::addLSUpdates(OspfBuilder& builder, Neighbor* nbr)
{
    // Filter for Intra lsas
    bool floodReduction = getFloodReduction();

    // Handle LSAs
    size_t sent{0};
    
    RetransmissionList<LsaKey, LsaRecordRef>& list = nbr
        ? nbr->getRtr().lsus() : multicastLsus;

    while (true)
    {
        LsaRecordRef record;
        if (!list.nextInBurst(record))
            break;

        auto& lsa = record.record;
        auto& key = record.key;
        if (!builder.hasRoom(lsa->header.length)) return sent;
        if (!buildLSAHeader(builder, key, *lsa, floodReduction)) return sent;
        if (!buildLSABody(builder, *lsa, key.lsaType)) return sent;

        builder.offset += (lsa->header.length - packet::Ospfv3LSAHeader::fixedSize);
        list.markBurst(key);
        sent++;
    }

    return sent;
}

void PacketDispatcherV3::buildDescriptions(OspfBuilder& builder, Neighbor& nbr)
{
    if (!nbr.currentDbd) return;

    auto& lsdb = getLsdb().getIterableLSDB();

    for (auto it = lsdb.upper_bound(*nbr.currentDbd); it != lsdb.end(); it++)
    {
        auto hdr = buildCopyLSAHeader(builder, it->first, it->second);
        if (!hdr.has_value())
        {
            if (it != lsdb.begin())
            {
                *nbr.currentDbd = std::prev(it)->first;
                return;
            }
        }
    }

    nbr.currentDbd = std::nullopt;
}

bool PacketDispatcherV3::buildLSABody(OspfBuilder& builder, const LsaRecord& record, uint16_t type)
{
    auto& body = record.body;
    uint16_t len = record.header.length - packet::Ospfv3LSAHeader::fixedSize;

    switch (type)
    {
        case OSPFV3_LSA_ROUTER:
            if (std::holds_alternative<RouterLsaV3>(body))
                return std::get<RouterLsaV3>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV3_LSA_NETWORK:
            if (std::holds_alternative<NetworkLsaV3>(body))
                return std::get<NetworkLsaV3>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV3_LSA_INTER_AREA_PREFIX:
            if (std::holds_alternative<InterAreaPrefixLsaV4>(body))
                return std::get<InterAreaPrefixLsaV4>(body).buildBody(builder.getBuf(), len);
            if (std::holds_alternative<InterAreaPrefixLsa>(body))
                return std::get<InterAreaPrefixLsa>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV3_LSA_INTER_AREA_ROUTER:
            if (std::holds_alternative<InterAreaRouterLsa>(body))
                return std::get<InterAreaRouterLsa>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV3_LSA_LINK:
            if (std::holds_alternative<LinkLsa>(body))
                return std::get<LinkLsa>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV3_LSA_INTRA_AREA_PREFIX:
            if (std::holds_alternative<IntraAreaPrefixLsaV4>(body))
                return std::get<IntraAreaPrefixLsaV4>(body).buildBody(builder.getBuf(), len);
            if (std::holds_alternative<IntraAreaPrefixLsa>(body))
                return std::get<IntraAreaPrefixLsa>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV3_LSA_AS_EXTERNAL:
        case OSPFV3_LSA_NSSA_EXTERNAL:
            if (std::holds_alternative<ExternalLsaV3>(body))
                return std::get<ExternalLsaV3>(body).buildBody(builder.getBuf(), len);
            break;
        default:
            return false;
    }
    return false;
}
} // namespace routing
