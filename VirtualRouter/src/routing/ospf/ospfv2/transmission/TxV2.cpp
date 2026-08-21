// Ospfv2Tx.cpp

// NOTE: set ttl to 1

#include "PacketDispatcherV2.h"
#include "processing/PacketBuilder.hpp"
#include "infrastructure/IPPacket.h"
#include "ospf/OspfProcess.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/transmission/OspfPacket.hpp"
#include "security/Encryption.hpp"

#include "ospf/database/LsdbTypes.hpp"
#include "ospf/area/Area.h"
#include "ospf/transmission/OspfFletcher.hpp"
#include "ospf/FlagManager.hpp"

#include "packet/headers/embedded/ospf/Ospfv2HelloHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv2DBDHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv2LSAHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv2LSRHeader.hpp"

namespace routing::ospf
{
uint16_t PacketDispatcherV2::getMtu()
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return 0;
    return txIface->configs.ipv4.mtu.load(std::memory_order_relaxed);
}

void PacketDispatcherV2::finalizeHeader(packet::Ospfv2Header& hdr, OspfBuilder& builder, bool lls, Neighbor* nbr)
{
    hdr.setPacketLen(static_cast<uint16_t>(builder.offset + packet::Ospfv2Header::fixedSize));

    bool restart = iface.getGracefulRestartInProgress();
    bool resync = nbr && nbr->resyncRequested.exchange(false, std::memory_order_relaxed);

    auto authField = getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_TYPE>();
    config::ospf::AuthType auth = authField.hasValue()
        ? authField.load() : config::ospf::AuthType::NULL_AUTH;

    if (auth == config::ospf::AuthType::CRYPTO)
    {
        auto id = getAuthKeyId();
        auto key = getAuthKey();
        if (id.has_value() && key.has_value())
        {
            uint32_t seq = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());

            // Add auth
            uint8_t secret[16] = {};
            utils::write<__uint128_t>(secret, key.value());
            if (!buildOspfCryptoAuthentication(builder, hdr, seq, id.value(), secret))
                return;

            if (lls)
            {
                uint8_t* llsBase = builder.getBuf();
                uint16_t size = addLinkLocalExtension(llsBase, restart, resync);
                builder.offset += size;
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
            uint16_t size = addLinkLocalExtension(buf, restart, resync);
            addLinkLocalChecksum(buf);
            builder.offset += size;
        }

        if (auth == config::ospf::AuthType::SIMPLE)
        {
            auto key = getIfaceGlobalBaseConfigs().get<config::OspfGlobalInterfaceBase::AUTHENTICATION_KEY>();
            if (key.hasValue()) buildOspfSimpleAuthentication(hdr, key.load());
        }

        // Crypto auth covers the checksum via HMAC, so it is only computed for NULL/SIMPLE.
        hdr.setChecksum(0);
        uint32_t sum = 0;
        auto addRange = [&sum](const uint8_t* p, size_t n) {
            size_t i = 0;
            for (; i + 1 < n; i += 2) sum += (static_cast<uint32_t>(p[i]) << 8) | p[i + 1];
            if (i < n) sum += static_cast<uint32_t>(p[i]) << 8;
        };
        addRange(hdr.buffer, 16); // version..authType, checksum field included as zero
        addRange(hdr.buffer + 24, builder.offset); // full trailing region (body + LLS), past the 8-byte auth field
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        hdr.setChecksum(static_cast<uint16_t>(~sum));
    }

    hdr.setTrailSize(builder.offset);
    builder.pkt.addTLVSize(builder.offset);
}

void PacketDispatcherV2::sendHello()
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return;
    processing::PacketBuilder pkt(txIface);

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.getLls();
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls);
    // Hello always goes to AllSPFRouters (RFC 2328 SS9.5) - unlike Update/LSAck,
    // it is not conditional on this router's DR/BDR status. A brand new
    // interface cannot be DR yet (DR election itself depends on Hello
    // exchange), so relying on transmit()'s DR-conditional default here sent
    // Hello to AllDRouters (224.0.0.6) instead, which other not-yet-DR/BDR
    // routers never join.
    types::IPAddress allSpfRouters(OSPFV2_ALL_SPF_ROUTERS, types::AddressFamily::IPv4);
    transmit(pkt, &allSpfRouters);
}

void PacketDispatcherV2::sendUnicastHello(Neighbor& nbr)
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return;
    processing::PacketBuilder pkt(txIface);

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_HELLO);
    if (!ospfHeader) return;

    uint16_t maxSize = static_cast<uint16_t>(pkt.getMaxHeaderSize(getMtu()));
    uint8_t* trail = ospfHeader->getTrailData();

    OspfBuilder builder{pkt, trail, 0, maxSize};

    bool lls = iface.getLls();
    if (!buildHello(builder, lls)) return;

    finalizeHeader(*ospfHeader, builder, lls, &nbr);
    transmit(pkt, &nbr.ipAddress);
}

void PacketDispatcherV2::sendInitDbd(Neighbor& nbr)
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return;
    processing::PacketBuilder pkt(txIface);

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_DATABASE_DESCRIPTION);
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

    builder.offset = packet::Ospfv2DBDHeader::fixedSize;
    setupDbd(nbr, ospfHeader.value());

    finalizeHeader(*ospfHeader, builder, lls);
    transmit(pkt, &nbr.ipAddress);
}

bool PacketDispatcherV2::sendDbd(Neighbor& nbr)
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return false;
    processing::PacketBuilder pkt(txIface);

    if (nbr.getRole() == Neighbor::Role::MASTER)
        nbr.currentSeq.fetch_add(1); // Add sequence if MASTER, otherwise ACK previous sequence.

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_DATABASE_DESCRIPTION);
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

bool PacketDispatcherV2::sendLsAck(Neighbor& nbr, std::vector<LsaRecordRef>& acks)
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return false;

    std::deque<processing::PacketBuilder> pkts;

    size_t sent = 0;
    while (sent < acks.size())
    {
        processing::PacketBuilder& pkt = pkts.emplace_back(txIface);

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
        transmit(pkt, &nbr.ipAddress);

    return true;
}

bool PacketDispatcherV2::sendLsr(Neighbor& nbr)
{
    auto& lsrs = nbr.getRtr().lsrs();
    if (!lsrs.burstActive()) lsrs.beginRetransmitBurst();

    auto request = buildLSRequest(nbr);
    if (!request.has_value()) return false;
    transmit(request.value(), &nbr.ipAddress);
    return true;
}

bool PacketDispatcherV2::sendLsu(Neighbor* nbr)
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
            static const types::IPAddress allDRouters(OSPFV2_ALL_D_ROUTERS, types::AddressFamily::IPv4);
            transmit(pkt.value(), &allDRouters);
        }
    }

    return true;
}

std::optional<processing::PacketBuilder> PacketDispatcherV2::buildLSRequest(Neighbor& nbr)
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return std::nullopt;
    processing::PacketBuilder pkt(txIface);

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_REQUEST);
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

std::optional<processing::PacketBuilder> PacketDispatcherV2::buildLSUpdate(Neighbor* nbr)
{
    auto* txIface = iface.getTransmitInterface();
    if (!txIface) return std::nullopt;
    processing::PacketBuilder pkt(txIface);

    auto ospfHeader = buildHeader(pkt, OSPFV2_TYPE_LINK_STATE_UPDATE);
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

std::optional<packet::Ospfv2Header> PacketDispatcherV2::buildHeader(processing::PacketBuilder& pkt, uint8_t type)
{
    infrastructure::ippacket::reserveIpv4(pkt);

    packet::Ospfv2Header ospf = pkt.reserveAndBuildHeader<packet::Ospfv2Header>(packet::HeaderType::OSPFV2);
    if (!ospf.buffer) return std::nullopt;

    ospf.setVersion(OSPFV2_VERSION);
    ospf.setType(type);
    ospf.setRouterID(iface.area.process.getRouterId());
    ospf.setAreaID(iface.getAreaId());

    return ospf;
}

std::optional<packet::Ospfv2HelloHeader> PacketDispatcherV2::buildHello(OspfBuilder& builder, bool lls)
{
    if (!builder.hasRoom(packet::Ospfv2HelloHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv2HelloHeader hello;
    hello.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv2HelloHeader::fixedSize;

    hello.setMask(iface.getTransmitAddress().getMask());
    hello.setHelloInterval(getHelloInterval());

    uint32_t options = getIfaceFlags();
    if (lls) AreaFlagManager::setLBitV2(options, true);
    hello.setOptions(static_cast<uint8_t>(options));

    hello.setPriority(iface.getPriority());
    hello.setDeadInterval(getDeadInterval());
    auto ntype = iface.getNetworkType();
    if (iface.getNetworkType() != config::ospf::NetworkType::POINT_TO_POINT)
    {
        hello.setDR(static_cast<OspfInterface*>(&iface)->getDrRid());
        hello.setBDR(static_cast<OspfInterface*>(&iface)->getBdrRid());
    }
    else
    {
        hello.setDR(0);
        hello.setBDR(0);
    }

    if (ntype == config::ospf::NetworkType::BROADCAST ||
        ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        auto result = ntable.addNeighborList(builder.getBuf(), builder.maxSize - builder.offset);
        if (!result.has_value()) return std::nullopt;
        builder.offset += result.value();
    }

    return hello;
}

std::optional<packet::Ospfv2DBDHeader> PacketDispatcherV2::buildDBD(OspfBuilder& builder, Neighbor& nbr, bool lls)
{
    if (!builder.hasRoom(packet::Ospfv2DBDHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv2DBDHeader dbd;
    dbd.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv2DBDHeader::fixedSize;

    uint32_t options = getIfaceFlags();
    if (lls) AreaFlagManager::setLBitV2(options, true);
    dbd.setOptions(static_cast<uint8_t>(options));

    dbd.setMtu(iface.getTransmitInterface()->configs.ipv4.mtu.load(std::memory_order_relaxed));
    dbd.setSequence(nbr.currentSeq.load(std::memory_order_relaxed));

    return dbd;
}

std::optional<packet::Ospfv2LSAHeader> PacketDispatcherV2::buildLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record, bool floodReduction)
{
    if (!builder.hasRoom(packet::Ospfv2LSAHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv2LSAHeader db;
    db.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv2LSAHeader::fixedSize;

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

std::optional<packet::Ospfv2LSAHeader> PacketDispatcherV2::buildCopyLSAHeader(OspfBuilder& builder, const LsaKey& key, const LsaRecord& record)
{
    if (!builder.hasRoom(packet::Ospfv2LSAHeader::fixedSize))
        return std::nullopt;

    packet::Ospfv2LSAHeader db;
    db.setBuffer(builder.getBuf());
    builder.offset += packet::Ospfv2LSAHeader::fixedSize;

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
        if (!builder.hasRoom(packet::Ospfv2LSRHeader::fixedSize))
            break;

        LsaKey key;
        if (!list.nextInBurst(key))
            break;

        packet::Ospfv2LSRHeader lsr;
        lsr.setBuffer(builder.getBuf());
        lsr.setType(key.lsaType);
        lsr.setLsID(key.linkStateId);
        lsr.setAdvRouter(key.advertisingRouter);
        builder.offset += packet::Ospfv2LSRHeader::fixedSize;
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
        if (!buildLSABody(builder, *lsa, static_cast<uint8_t>(key.lsaType))) return sent;

        builder.offset += (lsa->header.length - packet::Ospfv2LSAHeader::fixedSize);
        list.markBurst(key);
        sent++;
    }

    return sent;
}

void PacketDispatcherV2::buildDescriptions(OspfBuilder& builder, Neighbor& nbr)
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

bool PacketDispatcherV2::buildLLSAuthentication(OspfBuilder& info, uint16_t llsSize, uint32_t seq, uint8_t* secret)
{
    if (info.offset + 24 > info.maxSize) return false;
    uint8_t* llsBase = info.getBuf() - llsSize; // Start of the LLS Data Block (EO-TLV header)
    uint8_t* auth = info.getBuf();

    // Total LLS Data Block length, in 32-bit words, including this Auth TLV.
    utils::write<uint16_t>(llsBase + 2, (llsSize + 24) / 4);

    utils::write<uint16_t>(auth, 0x0002);
    utils::write<uint16_t>(auth + 2, 0x0014);
    utils::write<uint32_t>(auth + 4, seq);

    security::authentication::generateHMAC(auth + 8, llsBase, llsSize + 8, secret, 16, security::authentication::HmacType::MD5);

    info.offset += 24;
    return true;
}

void PacketDispatcherV2::buildOspfSimpleAuthentication(packet::Ospfv2Header& hdr, uint64_t secret)
{
    hdr.setAuthType(static_cast<uint16_t>(config::ospf::AuthType::SIMPLE));
    uint8_t* auth = hdr.getAuthentication();
    utils::write<uint64_t>(auth, secret);
}

bool PacketDispatcherV2::buildOspfCryptoAuthentication(OspfBuilder& info, packet::Ospfv2Header& hdr, uint32_t seq, uint8_t id, uint8_t* secret)
{
    hdr.setAuthType(static_cast<uint16_t>(config::ospf::AuthType::CRYPTO));
    if (info.offset + 16 > info.maxSize) return false;
    uint8_t* auth = hdr.getAuthentication();
    utils::write<uint16_t>(auth, 0);
    auth[2] = id;
    auth[3] = 0x10;
    utils::write<uint32_t>(auth + 4, seq);

    uint16_t packetLen = static_cast<uint16_t>(info.offset + packet::Ospfv2Header::fixedSize);
    security::authentication::generateHMAC(hdr.buffer + packetLen, hdr.buffer, packetLen, secret, 16, security::authentication::HmacType::MD5);
    info.offset += 16;
    return true;
}

bool PacketDispatcherV2::buildLSABody(OspfBuilder& builder, const LsaRecord& record, uint8_t type)
{
    auto& body = record.body;
    uint16_t len = record.header.length - packet::Ospfv2LSAHeader::fixedSize;

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
        case OSPFV2_LSA_OPAQUE_AREA:
        case OSPFV2_LSA_OPAQUE_AS:
            if (std::holds_alternative<OpaqueLsaV2>(body))
                return std::get<OpaqueLsaV2>(body).buildBody(builder.getBuf(), len);
            break;
        default:
            return false;
    }
    return false;
}
} // namespace routing
