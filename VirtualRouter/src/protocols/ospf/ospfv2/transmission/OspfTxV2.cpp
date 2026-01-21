// Ospfv2Tx.cpp

// NOTE: set ttl to 1

#include "PacketDispatcherV2.h"
#include <PacketBuilder.hpp>
#include <IPPacket.h>
#include <OspfProcess.h>
#include <OspfNeighbor.h>
#include <OspfPacket.hpp>

#include <LSDB.hpp>
#include <OspfArea.h>

#include <Ospfv2HelloHeader.hpp>
#include <Ospfv2DBDHeader.hpp>
#include <Ospfv2LSRHeader.hpp>
#include <Ospfv2LSAHeader.hpp>

#include <Ospfv3LSAHeader.hpp>

namespace OSPF
{
uint16_t PacketDispatcherV2::getMtu()
{
    return iface.getIface().configs.ipv4.mtu.load(std::memory_order_relaxed);
}

void PacketDispatcherV2::finalizeHeader(Ospfv2Header& hdr, OspfBuilder& builder, bool lls)
{
    hdr.setPacketLen(static_cast<uint16_t>(builder.offset + Ospfv2Header::fixedSize));

    if (lls)
    {
        uint8_t* buf = builder.getBuf();
        addLinkLocalExtension(buf, false);

        if (/*no auth*/true) // Auth does not require checksum
            addLinkLocalChecksum(buf);
    }

    // TODO: add auth to header
}

void PacketDispatcherV2::sendHello()
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.configs.lls.load(std::memory_order_relaxed);
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, OSPFV2_ALL_SPF_ROUTERS);
}

void PacketDispatcherV2::sendUnicastHello(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.configs.lls.load(std::memory_order_relaxed);
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, nbr.ipAddress.raw);
}

void PacketDispatcherV2::sendInitDBD(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_DATABASE_DESCRIPTION);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.configs.lls.load(std::memory_order_relaxed);
    auto dbd = buildDBD(builder, nbr, lls);
    if (!dbd.has_value()) return;

    dbd->setFlagI(true);
    dbd->setFlagM(true);
    dbd->setFlagMS(true);

    ospfHeader.value().setTrailSize(Ospfv2DBDHeader::fixedSize);
    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, nbr.ipAddress.raw);
}

bool PacketDispatcherV2::sendDBD(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    if (nbr.getRole() == Neighbor::Role::MASTER)
        nbr.currentSeq.fetch_add(1); // Add sequence if MASTER, otherwise ACK previous sequence.

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_DATABASE_DESCRIPTION);
    if (!ospfHeader) return false;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.configs.lls.load(std::memory_order_relaxed);
    auto db = buildDBD(builder, nbr, lls);
    if (!db.has_value()) return false;

    buildDescriptions(builder, nbr);
    if (nbr.currentDbd)
        db->setFlagM(true); // More to process

    ospfHeader.value().setTrailSize(builder.offset);
    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV2::sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& acks)
{
    std::deque<PacketBuilder> pkts;

    size_t sent = 0;
    while (sent < acks.size())
    {
        PacketBuilder& pkt = pkts.emplace_back(&iface.getIface());

        auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_ACK);
        if (!ospfHeader) return false;

        uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
        uint8_t* trail = ospfHeader->getTrailData();

        OspfBuilder builder{pkt, trail, 0, maxSize};

        using AckList = std::span<LsaRecordRef>;
        AckList ackList = AckList(acks.data() + sent, acks.size() - sent);
        size_t acksSent = buildLSAck(builder, ackList);
        if (acksSent == 0) return false;
        sent += acksSent;

        ospfHeader.value().setTrailSize(builder.offset);
        finalizeHeader(*ospfHeader, builder);
    }

    for (auto& pkt : pkts)
        transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV2::sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds)
{
    auto requests = buildLSRequestList(dbds);
    setupLsr(nbr, dbds);
    for (auto& pkt : requests)
        transmit(pkt, nbr.ipAddress.raw);
    
    return true;
}

bool PacketDispatcherV2::sendLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds)
{
    auto requests = buildLSRequestList(dbds);
    for (auto& pkt : requests)
        transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV2::sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys)
{
    std::vector<LsaRecordRef> sent;
    auto updates = buildLSUpdateList(keys, sent);

    if (nbr)
    {
        setupLsu(*nbr, sent);
        for (auto& pkt : updates)
            transmit(pkt, nbr->ipAddress.raw);
    }
    else
    {
        if (iface.isDr.load(std::memory_order_relaxed))
        {
            for (auto& pkt : updates)
                transmit(pkt, OSPFV2_ALL_SPF_ROUTERS);
        }
        else
        {
            for (auto& pkt : updates)
                transmit(pkt, OSPFV2_ALL_D_ROUTERS);
        }
    }

    return true;
}

bool PacketDispatcherV2::sendLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys)
{
    std::vector<LsaRecordRef> sent;
    auto updates = buildLSUpdateList(keys, sent);

    if (nbr)
    {
        for (auto& pkt : updates)
            transmit(pkt, nbr->ipAddress.raw);
    }
    else
    {
        if (iface.isDr.load(std::memory_order_relaxed))
        {
            for (auto& pkt : updates)
                transmit(pkt, OSPFV2_ALL_SPF_ROUTERS);
        }
        else
        {
            for (auto& pkt : updates)
                transmit(pkt, OSPFV2_ALL_D_ROUTERS);
        }
    }

    return true;
}

std::deque<PacketBuilder> PacketDispatcherV2::buildLSRequestList(const std::vector<LsaKey>& dbds)
{
    size_t sent = 0;

    std::deque<PacketBuilder> pkts;
    while (sent < dbds.size())
    {
        PacketBuilder& pkt = pkts.emplace_back(&iface.getIface());

        auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_REQUEST);
        if (!ospfHeader)
        {
            pkts.pop_back();
            return pkts;
        }

        uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
        uint8_t* trail = ospfHeader->getTrailData();

        OspfBuilder builder{pkt, trail, 0, maxSize};

        using ReqList = std::span<const LsaKey>;
        ReqList reqList = ReqList(dbds.data() + sent, dbds.size() - sent);
        size_t reqSent = buildLSRequest(builder, reqList);
        if (reqSent == 0)
        {
            pkts.pop_back();
            return pkts;
        }
        sent += reqSent;

        ospfHeader.value().setTrailSize(builder.offset);
        finalizeHeader(*ospfHeader, builder);
    }

    return pkts;
}

std::deque<PacketBuilder> PacketDispatcherV2::buildLSUpdateList(std::vector<std::pair<FloodInfo, LsaRecordRef>>& records, std::vector<LsaRecordRef>& sentKeys)
{
    size_t sent = 0;

    std::deque<PacketBuilder> pkts;
    sentKeys.reserve(records.size());

    while (sent < records.size())
    {
        PacketBuilder& pkt = pkts.emplace_back(&iface.getIface());

        auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_UPDATE);
        if (!ospfHeader)
        {
            pkts.pop_back();
            return pkts;
        }

        uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
        uint8_t* trail = ospfHeader->getTrailData();

        // Reserve 2 bytes for LSA count
        OspfBuilder builder{pkt, trail, 0, maxSize};
        builder.offset += 2;

        using UpdList = std::span<std::pair<FloodInfo, LsaRecordRef>>;
        UpdList updList = UpdList(records.data() + sent, records.size() - sent);

        auto updSent = buildLSUpdate(builder, sentKeys, updList);
        writeU16(trail, static_cast<uint16_t>(updSent));
        if (updSent == 0)
        {
            pkts.pop_back();
            return pkts;
        }
        sent += updSent;

        ospfHeader.value().setTrailSize(builder.offset);
        finalizeHeader(*ospfHeader, builder);
    }

    return pkts;
}

std::optional<Ospfv2Header> PacketDispatcherV2::buildHeader(PacketBuilder& pkt, uint8_t type)
{
    Protocol::IPPacket::reserveIpv4(pkt);

    Ospfv2Header ospf = pkt.reserveAndBuildHeader<Ospfv2Header>(HeaderType::IPV4);
    if (!ospf.buffer) return std::nullopt;

    ospf.setVersion(OSPFV2_VERSION);
    ospf.setType(type);
    ospf.setRouterID(iface.process.getRouterId());
    ospf.setAreaID(iface.getAreaId());

    return ospf;
}

std::optional<Ospfv2HelloHeader> PacketDispatcherV2::buildHello(OspfBuilder builder, bool lls)
{
    if (!builder.hasRoom(Ospfv2HelloHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv2HelloHeader::fixedSize;

    Ospfv2HelloHeader hello;
    hello.setBuffer(builder.getBuf());

    hello.setMask(iface.interfaceAddress.getMask());
    hello.setHelloInterval(iface.configs.helloInterval.load(std::memory_order_relaxed));

    uint8_t options = static_cast<uint8_t>(iface.getFlags().getFlags());
    if (lls) options |= 0x10;
    hello.setOptions(options);

    hello.setPriority(iface.configs.priority.load(std::memory_order_relaxed));
    hello.setDeadInterval(iface.configs.deadInterval.load(std::memory_order_relaxed));
    hello.setDR(static_cast<uint32_t>(iface.dr.rid.load(std::memory_order_relaxed)));
    hello.setBDR(static_cast<uint32_t>(iface.bdr.rid.load(std::memory_order_relaxed)));

    auto ntype = iface.configs.networkType.load(std::memory_order_relaxed);
    if (ntype == InterfaceConfigs::NetworkType::BROADCAST ||
        ntype == InterfaceConfigs::NetworkType::NON_BROADCAST)
    {
        auto result = ntable.addNeighborList(builder.getBuf(), builder.maxSize - builder.offset);
        if (!result.has_value()) return std::nullopt;
        builder.offset += result.value();
    }

    return hello;
}

std::optional<Ospfv2DBDHeader> PacketDispatcherV2::buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls)
{
    if (!builder.hasRoom(Ospfv2DBDHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv2DBDHeader::fixedSize;

    Ospfv2DBDHeader dbd;
    dbd.setBuffer(builder.getBuf());

    uint8_t options = static_cast<uint8_t>(iface.getFlags().getFlags());
    if (lls) options |= 0x10;
    dbd.setFlagI(options);

    dbd.setMtu(iface.getIface().configs.ipv4.mtu.load(std::memory_order_relaxed));
    dbd.setSequence(nbr.currentSeq.load(std::memory_order_relaxed));

    return dbd;
}

std::optional<Ospfv2LSAHeader> PacketDispatcherV2::buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record)
{
    if (!builder.hasRoom(Ospfv2LSAHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv2LSAHeader::fixedSize;

    Ospfv2LSAHeader db;
    db.setBuffer(builder.getBuf());

    if (iface.floodReduction.load(std::memory_order_relaxed))
    {
        // Set age to not expire
        db.setAge(0x8000);
    }
    else
    {
        uint16_t timeSinceRefresh = static_cast<uint16_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - record.lastRefreshTime
            ).count()
        );
        db.setAge(timeSinceRefresh + record.header.age);
    }


    // TODO: Options

    db.setType(static_cast<uint8_t>(key.lsaType));
    db.setLsID(key.linkStateId);
    db.setAdvRouter(key.advertisingRouter);
    db.setSeqNum(record.header.sequence);

    // TODO: handle checksum

    db.setLen(record.header.length);

    return db;
}

size_t PacketDispatcherV2::buildLSRequest(OspfBuilder& builder, std::span<const LsaKey>& keys)
{
    size_t sent{0};
    for (auto& key : keys)
    {
        if (!builder.hasRoom(Ospfv2LSRHeader::fixedSize))
            return sent;
        Ospfv2LSRHeader lsr;
        lsr.setBuffer(builder.getBuf());
        lsr.setType(key.lsaType);
        lsr.setLsID(key.linkStateId);
        lsr.setAdvRouter(key.advertisingRouter);
        builder.offset += Ospfv2LSRHeader::fixedSize;
        sent++;
    }
    return sent;
}

size_t PacketDispatcherV2::buildLSAck(OspfBuilder& builder, std::span<LsaRecordRef>& acks)
{
    size_t sent{0};
    for (auto& ack : acks)
    {
        auto lsa = buildLSAHeader(builder, ack.key, *ack.record);
        if (!lsa.has_value()) return sent;
        sent++;
    }
    return sent;
}

size_t PacketDispatcherV2::buildLSUpdate(OspfBuilder& builder, std::vector<LsaRecordRef>& sentKeys, std::span<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    // Filter for Intra lsas
    bool filter = iface.configs.databaseFilterOut.load(std::memory_order_relaxed);
    bool floodReduction = iface.floodReduction.load(std::memory_order_relaxed);

    // Handle LSAs
    size_t sent{0};
    for (auto& [info, record] : records)
    {
        if (!(filter || (floodReduction && info.reason == FloodReason::REFRESH)))
        {
            auto& lsa = record.record;
            auto& key = record.key;
            if (!builder.hasRoom(lsa->header.length)) return sent;
            if (!buildLSAHeader(builder, key, *lsa)) return sent;
            if (!buildLSABody(builder, *lsa, static_cast<uint8_t>(key.lsaType))) return sent;
            builder.offset += (lsa->header.length - Ospfv2LSAHeader::fixedSize);
            sentKeys.push_back(std::move(record));
        }
        sent++;
    }
    return sent;
}

void PacketDispatcherV2::buildDescriptions(OspfBuilder& builder, Neighbor& nbr)
{
    if (!nbr.currentDbd) return;

    auto& area = iface.getArea();
    std::shared_lock<std::shared_mutex> lk(area.lsdb().getLock());
    auto& lsdb = area.lsdb().getIterableLSDB();

    for (auto it = lsdb.upper_bound(*nbr.currentDbd); it != lsdb.end(); it++)
    {
        auto hdr = buildLSAHeader(builder, it->first, it->second);
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

bool PacketDispatcherV2::buildLSABody(OspfBuilder& builder, LsaRecord& record, uint8_t type)
{
    auto& body = record.body;
    uint16_t len = record.header.length - Ospfv2LSAHeader::fixedSize;

    switch (type)
    {
        case OSPFV2_LSA_ROUTER:
            if (std::holds_alternative<RouterLsaV2>(body))
                return std::get<RouterLsaV2>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV2_LSA_NETWORK:
            if (std::holds_alternative<NetworkLsaV2>(body))
                return std::get<NetworkLsaV2>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV2_LSA_SUM_NET:
            if (std::holds_alternative<SummaryNetworkLsa>(body))
                return std::get<SummaryNetworkLsa>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV2_LSA_SUM_ASBR:
            if (std::holds_alternative<SummaryRouterLsa>(body))
                return std::get<SummaryRouterLsa>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV2_LSA_EXTERNAL:
        case OSPFV2_LSA_NSSA:
            if (std::holds_alternative<ExternalLsaV2>(body))
                return std::get<ExternalLsaV2>(body).buildBody(builder.getBuf(), len);
            break;
        case OSPFV2_LSA_OPAQUE_LINK:
        {

        }
        case OSPFV2_LSA_OPAQUE_AREA:
        {

        }
        case OSPFV2_LSA_OPAQUE_AS:
        {

        }
        default:
            return false;
    }
    return false;
}
}
