// Ospfv2Tx.cpp

// NOTE: set ttl to 1

#include "PacketDispatcherV2.h"
#include <PacketBuilder.hpp>
#include <IPPacket.h>
#include <OspfProcess.h>
#include <OspfNeighbor.h>
#include <OspfPacket.hpp>
#include <Encryption.hpp>

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

    AuthType auth = baseConfigs->get<Config::OspfInterfaceBase::AUTHENTICATION_TYPE>().load();
    if (auth == AuthType::CRYPTO)
    {
        auto& id = baseConfigs->get<Config::OspfInterfaceBase::MESSAGE_DIGEST_KEY_ID>();
        auto& key = baseConfigs->get<Config::OspfInterfaceBase::MESSAGE_DIGEST_KEY>();
        if (id.hasValue() && key.hasValue())
        {
            uint32_t seq = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());

            // Add auth
            uint8_t secret[16] = {};
            writeU128(secret, key.load());
            if (!buildOspfCryptoAuthentication(builder, hdr, seq, id.load(), secret))
                return;

            if (lls)
            {
                uint16_t size = addLinkLocalExtension(builder.getBuf(), false);
                if (!buildLLSAuthentication(builder, size, seq, secret))
                    return;
            }
        }
    }
    else
    {
        if (lls)
        {
            uint8_t* buf = builder.getBuf();
            uint16_t size = addLinkLocalExtension(buf, false);
            addLinkLocalChecksum(buf);
            builder.offset += size;
        }

        if (auth == AuthType::SIMPLE)
        {
            auto& key = baseConfigs->get<Config::OspfInterfaceBase::AUTHENTICATION_KEY>();
            if (key.hasValue()) buildOspfSimpleAuthentication(hdr, key.load());
        }
    }

    hdr.setTrailSize(builder.offset);
}

void PacketDispatcherV2::sendHello()
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : iface.getProcess().getConfigs().get<Config::Ospf::LLS>().load();
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

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : iface.getProcess().getConfigs().get<Config::Ospf::LLS>().load();
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

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : iface.getProcess().getConfigs().get<Config::Ospf::LLS>().load();
    auto dbd = buildDBD(builder, nbr, lls);
    if (!dbd.has_value()) return;

    dbd->setFlagI(true);
    dbd->setFlagM(true);
    dbd->setFlagMS(true);

    builder.offset = Ospfv2DBDHeader::fixedSize;
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

    auto& ifaceLLS = baseConfigs->get<Config::OspfInterfaceBase::LLS>();
    bool lls = ifaceLLS.hasValue() ? ifaceLLS.load() : iface.getProcess().getConfigs().get<Config::Ospf::LLS>().load();
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
        size_t acksSent = addLSAcks(builder, ackList);
        if (acksSent == 0) return false;
        sent += acksSent;

        finalizeHeader(*ospfHeader, builder);
    }

    for (auto& pkt : pkts)
        transmit(pkt, nbr.ipAddress.raw);

    return true;
}

bool PacketDispatcherV2::sendLSRequest(Neighbor& nbr)
{
    auto request = buildLSRequest(nbr);
    if (!request.has_value()) return false;
    transmit(request.value(), nbr.ipAddress.raw);
    return true;
}

bool PacketDispatcherV2::sendLSUpdate(Neighbor* nbr)
{
    std::vector<LsaRecordRef> sent;
    auto pkt = buildLSUpdate(nbr);
    if (!pkt.has_value()) return false;

    if (nbr)
    {
        transmit(pkt.value(), nbr->ipAddress.raw);
    }
    else
    {
        if (iface.isDr.load(std::memory_order_relaxed))
        {
            transmit(pkt.value(), OSPFV2_ALL_SPF_ROUTERS);
        }
        else
        {
            transmit(pkt.value(), OSPFV2_ALL_D_ROUTERS);
        }
    }

    return true;
}

std::optional<PacketBuilder> PacketDispatcherV2::buildLSRequest(Neighbor& nbr)
{
    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_REQUEST);
    if (!ospfHeader)
    {
        iface.getIface().tx->release(pkt.frame);
        return std::nullopt;
    }

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    size_t sent = addLSRequests(builder, nbr);
    if (sent == 0) 
    {
        iface.getIface().tx->release(pkt.frame);
        return std::nullopt;
    }

    finalizeHeader(*ospfHeader, builder);

    return pkt;
}

std::optional<PacketBuilder> PacketDispatcherV2::buildLSUpdate(Neighbor* nbr)
{
    size_t sent = 0;

    PacketBuilder pkt(&iface.getIface());

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_UPDATE);
    if (!ospfHeader)
    {
        iface.getIface().tx->release(pkt.frame);
        return std::nullopt;
    }

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    // Reserve 2 bytes for LSA count
    OspfBuilder builder{pkt, trail, 0, maxSize};
    builder.offset += 2;

    auto updSent = addLSUpdates(builder, nbr);
    if (sent == 0) return std::nullopt;
    writeU16(trail, static_cast<uint16_t>(updSent));
    if (updSent == 0)
    {
        iface.getIface().tx->release(pkt.frame);
        return std::nullopt;
    }
    sent += updSent;

    finalizeHeader(*ospfHeader, builder);

    return pkt;
}

std::optional<Ospfv2Header> PacketDispatcherV2::buildHeader(PacketBuilder& pkt, uint8_t type)
{
    Protocol::IPPacket::reserveIpv4(pkt);

    Ospfv2Header ospf = pkt.reserveAndBuildHeader<Ospfv2Header>(HeaderType::IPV4);
    if (!ospf.buffer) return std::nullopt;

    ospf.setVersion(OSPFV2_VERSION);
    ospf.setType(type);
    ospf.setRouterID(iface.getProcess().getRouterId());
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
    hello.setHelloInterval(configs->get<Config::OspfInterface::HELLO_INTERVAL>().load());

    uint8_t options = static_cast<uint8_t>(iface.getFlags().getFlags());
    if (lls) options |= 0x10;
    hello.setOptions(options);

    hello.setPriority(configs->get<Config::OspfInterface::PRIORITY>().load());
    hello.setDeadInterval(configs->get<Config::OspfInterface::DEAD_INTERVAL>().load());
    hello.setDR(static_cast<uint32_t>(iface.dr.rid.load(std::memory_order_relaxed)));
    hello.setBDR(static_cast<uint32_t>(iface.bdr.rid.load(std::memory_order_relaxed)));

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

std::optional<Ospfv2LSAHeader> PacketDispatcherV2::buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction)
{
    if (!builder.hasRoom(Ospfv2LSAHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv2LSAHeader::fixedSize;

    Ospfv2LSAHeader db;
    db.setBuffer(builder.getBuf());

    // Check if self originated
    db.setAge(calculateAge(floodReduction, record));
    db.setOptions(record.header.options);
    db.setType(static_cast<uint8_t>(key.lsaType));
    db.setLsID(key.linkStateId);
    db.setAdvRouter(key.advertisingRouter);
    db.setSeqNum(record.header.sequence);

    auto calcs = runLsaCalculations<PolicyV2>(record.header, key, record.body);
     
    db.setChecksum(calcs.checksum);
    db.setLen(calcs.size);

    return db;
}

std::optional<Ospfv2LSAHeader> PacketDispatcherV2::buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record)
{
    if (!builder.hasRoom(Ospfv2LSAHeader::fixedSize))
        return std::nullopt;
    builder.offset += Ospfv2LSAHeader::fixedSize;

    Ospfv2LSAHeader db;
    db.setBuffer(builder.getBuf());

    db.setAge(record.header.age);
    db.setOptions(record.header.options);
    db.setType(static_cast<uint8_t>(key.lsaType));
    db.setLsID(key.linkStateId);
    db.setAdvRouter(key.advertisingRouter);
    db.setSeqNum(record.header.sequence);
    db.setChecksum(record.header.checksum);
    db.setLen(record.header.length);

    return db;
}

size_t PacketDispatcherV2::addLSRequests(OspfBuilder& builder, Neighbor& nbr)
{
    size_t sent{0};
    auto& list = nbr.getRtr().lsrs();

    while (true)
    {
        if (!builder.hasRoom(Ospfv2LSRHeader::fixedSize))
            break;

        LsaKey key;
        if (!list.nextInBurst(key))
            break;

        Ospfv2LSRHeader lsr;
        lsr.setBuffer(builder.getBuf());
        lsr.setType(key.lsaType);
        lsr.setLsID(key.linkStateId);
        lsr.setAdvRouter(key.advertisingRouter);
        builder.offset += Ospfv2LSRHeader::fixedSize;
        list.markBurst(key);
        sent++;
    }
    return sent;
}

size_t PacketDispatcherV2::addLSAcks(OspfBuilder& builder, std::span<LsaRecordRef>& acks)
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

size_t PacketDispatcherV2::addLSUpdates(OspfBuilder& builder, Neighbor* nbr)
{
    // Filter for Intra lsas
    bool floodReduction = iface.floodReduction.load(std::memory_order_relaxed);

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
        if (!buildLSABody(builder, *lsa, static_cast<uint8_t>(key.lsaType))) return sent;

        builder.offset += (lsa->header.length - Ospfv2LSAHeader::fixedSize);
        list.markBurst(key);
        sent++;
    }

    return sent;
}

void PacketDispatcherV2::buildDescriptions(OspfBuilder& builder, Neighbor& nbr)
{
    if (!nbr.currentDbd) return;

    auto& area = iface.getArea();
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

bool PacketDispatcherV2::buildLLSAuthentication(OspfBuilder& info, uint16_t llsSize, uint32_t seq, uint8_t* secret)
{
    if (info.offset + llsSize + 24 > info.maxSize) return false;
    uint8_t* lls = info.getBuf() + info.offset;
    writeU16(lls + 2, llsSize + 24/*Auth tlv size*/);

    writeU16(lls, 0x0002);
    writeU16(lls + 2, 0x0014);
    writeU32(lls + 4, seq);
    
    Authentication::generateHMAC(lls + 8, lls, llsSize + 8, secret, 16, Authentication::HmacType::MD5);

    info.offset += llsSize + 24;
    return true;
}

void PacketDispatcherV2::buildOspfSimpleAuthentication(Ospfv2Header& hdr, uint64_t secret)
{
    hdr.setAuthType(static_cast<uint16_t>(AuthType::SIMPLE));
    uint8_t* auth = hdr.getAuthentication();
    writeU64(auth, secret);
}

bool PacketDispatcherV2::buildOspfCryptoAuthentication(OspfBuilder& info, Ospfv2Header& hdr, uint32_t seq, uint8_t id, uint8_t* secret)
{
    hdr.setAuthType(static_cast<uint16_t>(AuthType::CRYPTO));
    if (info.offset + 16 > info.maxSize) return false;
    uint8_t* auth = hdr.getAuthentication();
    writeU16(auth, 0);
    auth[2] = id;
    auth[3] = 0x10;
    writeU32(auth + 4, seq);
    Authentication::generateHMAC(info.getBuf() + info.offset, info.getBuf(), info.offset, secret, 16, Authentication::HmacType::MD5);
    return true;
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
