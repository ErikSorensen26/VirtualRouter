// Ospfv3Tx.cpp

// NOTE: set ttl to 1

#include "PacketDispatcherV3.h"
#include <PacketBuilder.hpp>
#include <IPPacket.h>
#include <OspfProcess.h>
#include <OspfNeighbor.h>
#include <OspfPacket.hpp>
#include <Encryption.hpp>

#include <LSDB.hpp>
#include <OspfArea.h>

#include <Ospfv3Header.hpp>
#include <Ospfv3HelloHeader.hpp>
#include <Ospfv3DBDHeader.hpp>
#include <Ospfv3LSRHeader.hpp>
#include <Ospfv3LSAHeader.hpp>

#include <Ospfv3LSAHeader.hpp>

namespace OSPF
{
uint16_t PacketDispatcherV3::getMtu()
{
    return iface.getIface().configs.ipv4.mtu.load(std::memory_order_relaxed);
}

void PacketDispatcherV3::finalizeHeader(Ospfv3Header& hdr, OspfBuilder& builder, bool lls)
{
    hdr.setPacketLen(static_cast<uint16_t>(builder.offset + Ospfv3Header::fixedSize));

    if (lls)
    {
        uint8_t* buf = builder.getBuf();
        uint16_t size = addLinkLocalExtension(buf, false);
        addLinkLocalChecksum(buf);
        builder.offset += size;
    }

    hdr.setTrailSize(builder.offset);
}

void PacketDispatcherV3::sendHello()
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : false;
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, OSPFV3_ALL_SPF_ROUTERS);
}

void PacketDispatcherV3::sendUnicastHello(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : false;
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, nbr.ipAddress.raw);
}

void PacketDispatcherV3::sendInitDBD(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_DATABASE_DESCRIPTION);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : false;
    auto dbd = buildDBD(builder, nbr, lls);
    if (!dbd.has_value()) return;

    dbd->setFlagI(true);
    dbd->setFlagM(true);
    dbd->setFlagMS(true);

    builder.offset = Ospfv3DBDHeader::fixedSize;
    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, nbr.ipAddress.raw);
}

bool PacketDispatcherV3::sendDBD(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    if (nbr.getRole() == Neighbor::Role::MASTER)
        nbr.currentSeq.fetch_add(1); // Add sequence if MASTER, otherwise ACK previous sequence.

    auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_DATABASE_DESCRIPTION);
    if (!ospfHeader) return false;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : false;
    auto db = buildDBD(builder, nbr, lls);
    if (!db.has_value()) return false;

    buildDescriptions(builder, nbr);
    if (nbr.currentDbd)
        db->setFlagM(true); // More to process

    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV3::sendLSAck(Neighbor& nbr, std::vector<LsaRecordRef>& acks)
{
    std::deque<PacketBuilder> pkts;

    size_t sent = 0;
    while (sent < acks.size())
    {
        PacketBuilder& pkt = pkts.emplace_back(&iface.getIface());

        auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_LINK_STATE_ACK);
        if (!ospfHeader) return false;

        uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
        uint8_t* trail = ospfHeader->getTrailData();

        OspfBuilder builder{pkt, trail, 0, maxSize};

        using AckList = std::span<LsaRecordRef>;
        AckList ackList = AckList(acks.data() + sent, acks.size() - sent);
        size_t acksSent = buildLSAck(builder, ackList);
        if (acksSent == 0) return false;
        sent += acksSent;

        finalizeHeader(*ospfHeader, builder);
    }

    for (auto& pkt : pkts)
        transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV3::sendReliableLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds)
{
    auto requests = buildLSRequestList(dbds);
    setupLsr(nbr, dbds);
    for (auto& pkt : requests)
        transmit(pkt, nbr.ipAddress.raw);
    
    return true;
}

bool PacketDispatcherV3::sendLSRequest(Neighbor& nbr, const std::vector<LsaKey>& dbds)
{
    auto requests = buildLSRequestList(dbds);
    for (auto& pkt : requests)
        transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV3::sendReliableLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys)
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
                transmit(pkt, OSPFV3_ALL_SPF_ROUTERS);
        }
        else
        {
            for (auto& pkt : updates)
                transmit(pkt, OSPFV3_ALL_D_ROUTERS);
        }
    }

    return true;
}

bool PacketDispatcherV3::sendLSUpdate(Neighbor* nbr, std::vector<std::pair<FloodInfo, LsaRecordRef>>& keys)
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
                transmit(pkt, OSPFV3_ALL_SPF_ROUTERS);
        }
        else
        {
            for (auto& pkt : updates)
                transmit(pkt, OSPFV3_ALL_D_ROUTERS);
        }
    }

    return true;
}

std::deque<PacketBuilder> PacketDispatcherV3::buildLSRequestList(const std::vector<LsaKey>& dbds)
{
    size_t sent = 0;

    std::deque<PacketBuilder> pkts;
    while (sent < dbds.size())
    {
        PacketBuilder& pkt = pkts.emplace_back(&iface.getIface());

        auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_LINK_STATE_REQUEST);
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

        finalizeHeader(*ospfHeader, builder);
    }

    return pkts;
}

std::deque<PacketBuilder> PacketDispatcherV3::buildLSUpdateList(std::vector<std::pair<FloodInfo, LsaRecordRef>>& records, std::vector<LsaRecordRef>& sentKeys)
{
    size_t sent = 0;

    std::deque<PacketBuilder> pkts;
    sentKeys.reserve(records.size());

    while (sent < records.size())
    {
        PacketBuilder& pkt = pkts.emplace_back(&iface.getIface());

        auto ospfHeader = buildHeader(pkt, OSPFV3_TYPE_LINK_STATE_UPDATE);
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

        finalizeHeader(*ospfHeader, builder);
    }

    return pkts;
}

std::optional<Ospfv3Header> PacketDispatcherV3::buildHeader(PacketBuilder& pkt, uint8_t type)
{
    Protocol::IPPacket::reserveIpv4(pkt);

    Ospfv3Header ospf = pkt.reserveAndBuildHeader<Ospfv3Header>(HeaderType::IPV4);
    if (!ospf.buffer) return std::nullopt;

    ospf.setVersion(OSPFV3_VERSION);
    ospf.setType(type);
    ospf.setRouterID(iface.getProcess().getRouterId());
    ospf.setAreaID(iface.getAreaId());

    return ospf;
}

std::optional<Ospfv3HelloHeader> PacketDispatcherV3::buildHello(OspfBuilder builder, bool lls)
{
    if (!builder.hasRoom(Ospfv3HelloHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv3HelloHeader::fixedSize;

    Ospfv3HelloHeader hello;
    hello.setBuffer(builder.getBuf());

    hello.setInterfaceID(iface.interfaceId);
    hello.setHelloInterval(configs->get<Config::OspfInterface::HELLO_INTERVAL>().load());

    uint8_t options = static_cast<uint8_t>(iface.getFlags().getFlags());
    if (lls) options |= 0x10;
    hello.setOptions(options);

    hello.setRouterPriority(configs->get<Config::OspfInterface::PRIORITY>().load());
    hello.setDeadInterval(configs->get<Config::OspfInterface::DEAD_INTERVAL>().load());
    hello.setDrID(static_cast<uint32_t>(iface.dr.rid.load(std::memory_order_relaxed)));
    hello.setBdrID(static_cast<uint32_t>(iface.bdr.rid.load(std::memory_order_relaxed)));

    auto ntype = configs->get<Config::OspfInterface::NETWORK>().load();
    if (ntype == NetworkType::BROADCAST ||
        ntype == NetworkType::NON_BROADCAST)
    {
        auto result = ntable.addNeighborList(builder.getBuf(), builder.maxSize - builder.offset);
        if (!result.has_value()) return std::nullopt;
        builder.offset += result.value();
    }

    return hello;
}

std::optional<Ospfv3DBDHeader> PacketDispatcherV3::buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls)
{
    if (!builder.hasRoom(Ospfv3DBDHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv3DBDHeader::fixedSize;

    Ospfv3DBDHeader dbd;
    dbd.setBuffer(builder.getBuf());

    uint8_t options = static_cast<uint8_t>(iface.getFlags().getFlags());
    if (lls) options |= 0x10;
    dbd.setFlagI(options);

    dbd.setMtu(iface.getIface().configs.ipv4.mtu.load(std::memory_order_relaxed));
    dbd.setSequence(nbr.currentSeq.load(std::memory_order_relaxed));

    return dbd;
}

std::optional<Ospfv3LSAHeader> PacketDispatcherV3::buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction)
{
    if (!builder.hasRoom(Ospfv3LSAHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv3LSAHeader::fixedSize;

    Ospfv3LSAHeader db;
    db.setBuffer(builder.getBuf());

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

std::optional<Ospfv3LSAHeader> PacketDispatcherV3::buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record)
{
    if (!builder.hasRoom(Ospfv3LSAHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv3LSAHeader::fixedSize;

    Ospfv3LSAHeader db;
    db.setBuffer(builder.getBuf());

    db.setAge(record.header.age);
    db.setType(static_cast<uint8_t>(key.lsaType));
    db.setLsID(key.linkStateId);
    db.setAdvRouter(key.advertisingRouter);
    db.setSeqNum(record.header.sequence);
    db.setChecksum(record.header.checksum);
    db.setLen(record.header.length);

    return db;
}

size_t PacketDispatcherV3::buildLSRequest(OspfBuilder& builder, std::span<const LsaKey>& keys)
{
    size_t sent{0};
    for (auto& key : keys)
    {
        if (!builder.hasRoom(Ospfv3LSRHeader::fixedSize))
            return sent;
        Ospfv3LSRHeader lsr;
        lsr.setBuffer(builder.getBuf());
        lsr.setType(key.lsaType);
        lsr.setLsID(key.linkStateId);
        lsr.setAdvRouter(key.advertisingRouter);
        builder.offset += Ospfv3LSRHeader::fixedSize;
        sent++;
    }
    return sent;
}

size_t PacketDispatcherV3::buildLSAck(OspfBuilder& builder, std::span<LsaRecordRef>& acks)
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

size_t PacketDispatcherV3::buildLSUpdate(OspfBuilder& builder, std::vector<LsaRecordRef>& sentKeys, std::span<std::pair<FloodInfo, LsaRecordRef>>& records)
{
    // Filter for Intra lsas
    bool filter = configs->get<Config::OspfInterface::DATABASE_FILTER>().load();
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
            if (!buildLSAHeader(builder, key, *lsa, floodReduction)) return sent;
            if (!buildLSABody(builder, *lsa, static_cast<uint8_t>(key.lsaType))) return sent;
            builder.offset += (lsa->header.length - Ospfv3LSAHeader::fixedSize);
            sentKeys.push_back(std::move(record));
        }
        sent++;
    }
    return sent;
}

void PacketDispatcherV3::buildDescriptions(OspfBuilder& builder, Neighbor& nbr)
{
    if (!nbr.currentDbd) return;

    auto& area = iface.getArea();
    std::shared_lock<std::shared_mutex> lk(area.lsdb().getLock());
    auto& lsdb = area.lsdb().getIterableLSDB();

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

bool PacketDispatcherV3::buildLSABody(OspfBuilder& builder, LsaRecord& record, uint8_t type)
{
    auto& body = record.body;
    uint16_t len = record.header.length - Ospfv3LSAHeader::fixedSize;

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
}

