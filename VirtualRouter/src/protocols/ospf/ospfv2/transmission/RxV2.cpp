// Ospfv2Rx.cpp

#include "PacketDispatcherV2.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/interface/InterfaceTimers.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/area/Area.h"
#include "ospf/area/FlagManager.h"

#include "packet/headers/embedded/ospf/Ospfv2HelloHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv2DBDHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv2LSAHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv2LSRHeader.hpp"

#include "security/Encryption.hpp"

namespace OSPF
{
Config::OspfInterfaceBaseRegistry& PacketDispatcherV2::getBaseConfigs()
{
    return baseConfigs.get();
}

void PacketDispatcherV2::handleIncoming(const Ospfv2Header& ospfHeader, const uint8_t* neighborIp, bool multicast)
{
    IPAddress neigIp(neighborIp, iface.getProcess().getAF());
    uint32_t rid = ospfHeader.getRouterID();

    // Check passive
    if (iface.getConfigs().get<Config::OspfInterface::PASSIVE>().load())
        return;

    // Validate version
    if (ospfHeader.getVersion() != OSPFV2_VERSION)
        return;

    if (ospfHeader.getAreaID() != iface.getAreaId())
        return;

    // Validate size
    size_t packetSize = Ospfv2Header::fixedSize + ospfHeader.getTrail().size();
    if (ospfHeader.getPacketLen() > packetSize) return;

    auto& interfaceAuth = baseConfigs->get<Config::OspfInterfaceBase::AUTHENTICATION_TYPE>();
    auto authType = interfaceAuth.hasValue() ? interfaceAuth.load()
        : iface.getArea().getConfigs().get<Config::OspfArea::AUTHENTICATION_TYPE>().load();

    HeaderInfo info(ospfHeader.getTrail().data(), packetSize, ospfHeader.getPacketLen(), static_cast<uint8_t>(authType), neigIp, rid);
    info.neighbor = iface.getNTable().lookup(rid);

    switch (authType)
    {
        case AuthType::CRYPTO:
            if (!processOspfCryptoAuthentication(info, ospfHeader))
                return;
            break; // Break to skip checksum (crypto covers checkum).
        case AuthType::SIMPLE:
            if (!processOspfSimpleAuthentication(ospfHeader))
                return;
            // Simple still requires checksum so no break.
        default:
        {
            // Process Checksum
            ChecksumFletcher check;
            check.addBytes(ospfHeader.buffer, 12); // Up to checksum field
            check.addBytes(ospfHeader.buffer + 14, ospfHeader.getPacketLen() - 14); // To end of header
            uint16_t checksum = check.finalize();
            if (checksum != ospfHeader.getChecksum())
                return; // Invalid checksum
        }
    }

    // TODO header stuff

    if (ospfHeader.getType() == OSPFV2_TYPE_HELLO)
    {
        if (info.neighbor && info.neighbor->unicast == multicast)
            return;
        processHello(info, !multicast);
    }
    else if (info.neighbor)
    {
        switch (ospfHeader.getType())
        {
            case OSPFV2_TYPE_DATABASE_DESCRIPTION:
                processDBD(info);
                break;
            case OSPFV2_TYPE_LINK_STATE_ACK:
                processLSAck(info);
                break;
            case OSPFV2_TYPE_LINK_STATE_REQUEST:
                processLSRequest(info);
                break;
            case OSPFV2_TYPE_LINK_STATE_UPDATE:
                processLSUpdate(info);
                break;
        }
    }
}

bool PacketDispatcherV2::processOptions(uint32_t options, Neighbor& nbr)
{
    (void)nbr; // For OSPFV3

    auto& flags = iface.getFlags();
    auto& areaFlags = iface.getArea().getFlags();

    if (iface.demandCircuit == OspfInterface::DcDecision::UNDECIDED)
    {
        if (InterfaceFlagManager::getDemandCircuits(options) && flags.getDemandCircuits() &&
            iface.getConfigs().get<Config::OspfInterface::NETWORK>().load() == NetworkType::POINT_TO_POINT)
            iface.demandCircuit = OspfInterface::DcDecision::ENABLED;
        else
            iface.demandCircuit = OspfInterface::DcDecision::DISABLED;
    }
    if (iface.opaqueEnabled.load(std::memory_order_relaxed) && AreaFlagManager::getOpaque(options))
        iface.opaqueEnabled.store(false, std::memory_order_relaxed);
    if (areaFlags.getExternalRouting() != AreaFlagManager::getExternalRouting(options))
        return false;
    if (areaFlags.getNssa() != AreaFlagManager::getNssa(options))
        return false;
    if (areaFlags.getAddressFamilySupport() != AreaFlagManager::getAddressFamilySupport(options) && iface.getProcess().getAF() == AddressFamily::IPv4)
        return false;
    return true;
}

void PacketDispatcherV2::processHello(PacketDispatcher::HeaderInfo& info, bool unicast)
{
    Ospfv2HelloHeader hdr;
    hdr.setBuffer(info.payload);

    auto& ifaceConfigs = iface.getConfigs();

    info.offset += Ospfv2HelloHeader::fixedSize;
    if (info.offset > info.payloadSize)
        return;

    // Validate timers
    if (hdr.getHelloInterval() != ifaceConfigs.get<Config::OspfInterface::HELLO_INTERVAL>().load() ||
        hdr.getDeadInterval() != ifaceConfigs.get<Config::OspfInterface::DEAD_INTERVAL>().load())
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    // Store options
    if (info.neighbor->getState() != Neighbor::State::FULL && !processOptions(static_cast<uint32_t>(hdr.getOptions()), *info.neighbor))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    auto ntype = ifaceConfigs.get<Config::OspfInterface::NETWORK>().load();
    bool multiAccess = ntype == NetworkType::BROADCAST || ntype == NetworkType::NON_BROADCAST;

    if (multiAccess)
    {
        if (hdr.getMask() != iface.interfaceAddress.getMask())
        {
            info.neighbor->setState(Neighbor::State::DOWN);
            return;
        }
    }

    if (!info.neighbor)
    {
        info.neighbor = ntable.createNeighbor(info.rid, info.neighborIp);
    }
    else if (unicast && info.neighbor)
    {
        auto state = info.neighbor->getState();
        if (state == Neighbor::State::DOWN || state == Neighbor::State::ATTEMPT)
            info.neighbor->setState(Neighbor::State::INIT);
    }

    if (ntype == NetworkType::BROADCAST || ntype == NetworkType::NON_BROADCAST)
    {
        uint8_t* neighborList = info.payload + Ospfv2HelloHeader::fixedSize;
        size_t listSize = info.payloadSize - Ospfv2HelloHeader::fixedSize;
        if (listSize % 4 != 0) return;

        // Find RID
        bool ridFound = false;
        for (size_t i = 0; i < listSize; i += 4)
        {
            if (readU32(neighborList + i) == iface.getProcess().getRouterId())
            {
                ridFound = true;
                break;
            }
        }

        // Handle two way
        if (!ridFound) return;
        if (ridFound && info.neighbor->getState() == Neighbor::State::INIT)
            info.neighbor->setState(Neighbor::State::TWOWAY);
    }
    else if (info.neighbor->getState() == Neighbor::State::INIT)
    {
        info.neighbor->setState(Neighbor::State::TWOWAY);
    }

    iface.getTimers().startInactiveTimer(*info.neighbor);

    // Update neighbor variables
    if (info.neighbor->priority.load(std::memory_order_relaxed) != hdr.getPriority())
        info.neighbor->priority.store(hdr.getPriority(), std::memory_order_release);

    if (multiAccess)
    {
        // Read new values from the Hello
        const uint8_t  newPriority = hdr.getPriority();
        const uint32_t newDr       = hdr.getDR();
        const uint32_t newBdr      = hdr.getBDR();

        // Update neighbor-advertised DR/BDR and priority from the Hello
        info.neighbor->priority.store(newPriority, std::memory_order_relaxed);
        info.neighbor->dr.store(newDr, std::memory_order_relaxed);
        info.neighbor->bdr.store(newBdr, std::memory_order_relaxed);

        uint32_t currentDr = iface.dr.rid.load(std::memory_order_relaxed);
        uint32_t currentBdr = iface.bdr.rid.load(std::memory_order_relaxed);

        const bool election = (newPriority == 0 &&
            (info.neighbor->routerID == currentBdr ||
             info.neighbor->routerID == currentDr)) ||
            (info.neighbor->getState() == Neighbor::State::TWOWAY &&
            ((iface.dr.rid.load(std::memory_order_relaxed) == 0) ||
            (iface.bdr.rid.load(std::memory_order_relaxed) == 0)));

        if (election)
            iface.election();
    }
}

void PacketDispatcherV2::processDBD(PacketDispatcher::HeaderInfo& info)
{
    Area& area = iface.getArea();

    auto state = info.neighbor->getState();
    if (state < Neighbor::State::EXSTART) return;

    Ospfv2DBDHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv2DBDHeader::fixedSize;
    if (info.offset > info.payloadSize) 
        return;

    // Varify only 20 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 20 != 0)
        return;

    uint8_t options = hdr.getOptions();
    if (!processOptions(options, *info.neighbor))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    // Verify MTU
    if (iface.getConfigs().get<Config::OspfInterface::MTU_IGNORE>().load() && info.neighbor->mtu != hdr.getMtu())
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    bool ack = hdr.getSequence() == info.neighbor->currentSeq.load(std::memory_order_relaxed);
    bool init = hdr.getFlagI();

    if (init)
    {
        // Run INIT
        if (!hdr.getFlagI())
            return;
        if (info.offset != info.payloadSize)
            return;

        if (info.offset != info.payloadSize)
            return;

        Neighbor::Role role = info.neighbor->routerID > iface.getProcess().getRouterId()
            ? Neighbor::Role::MASTER
            : Neighbor::Role::SLAVE;
        info.neighbor->setRole(role);

        if (role == Neighbor::Role::SLAVE)
            info.neighbor->currentSeq.store(hdr.getSequence());

        else if (state != Neighbor::State::EXSTART)
            // Move to EXSTART, will send init either acking or setting initial sequence.
            info.neighbor->setState(Neighbor::State::EXSTART);

        if (role == Neighbor::Role::SLAVE) // SLAVE
        {
            // Ack MASTERs init, move to EXCHANGE, and await MASTER.
            sendInitDBD(*info.neighbor);
            info.neighbor->setState(Neighbor::State::EXCHANGE);
        }
        else if (ack) // MASTER and Ack received, move to EXCHANGE.
        {
            info.neighbor->setState(Neighbor::State::EXCHANGE);
        }
    }
    else if (state == Neighbor::State::EXCHANGE)
    {
        // Process DB description.
        auto& rtr = info.neighbor->getRtr();
        while (info.offset < info.payloadSize)
        {
            Ospfv2LSAHeader lsaHdr;
            lsaHdr.setBuffer(info.payload + info.offset);

            info.offset += Ospfv2LSAHeader::fixedSize;
            if (info.offset > info.payloadSize) 
                return;

            LsaKey key(lsaHdr.getType(), lsaHdr.getLsID(), lsaHdr.getAdvRouter());

            LsaHeader lsa = {
                .sequence = lsaHdr.getSeqNumber(),
                .checksum = lsaHdr.getChecksum(),
                .length = lsaHdr.getLen(),
                .age = lsaHdr.getAge()
            };

            if (area.compareLSASummary(lsa, key))
                rtr.lsrs().add(key, key);
        }

        if ((info.neighbor->currentDbd.has_value() || hdr.getFlagM()) || info.neighbor->getRole() == Neighbor::Role::SLAVE)
        {
            sendDBD(*info.neighbor);
        }
        else
        {
            if (info.neighbor->getRole() == Neighbor::Role::SLAVE)
                sendDBD(*info.neighbor); // Respond to MASTER even if no lsas to process

            // Move to LOADING
            info.neighbor->setState(Neighbor::State::LOADING); 
        }
    }

    if (AreaFlagManager::getExternalAttribute(options))
        processLLSDataBlock(info);
}

void PacketDispatcherV2::processLSAck(HeaderInfo& info)
{
    // Process ACKs.
    auto& rtr = info.neighbor->getRtr();
    while (info.offset < info.payloadSize)
    {
        Ospfv2LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv2LSAHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(lsaHdr.getType(), lsaHdr.getLsID(), lsaHdr.getAdvRouter());

        LsaHeader lsa = {
            .sequence = lsaHdr.getSeqNumber(),
            .checksum = lsaHdr.getChecksum(),
            .length = lsaHdr.getLen(),
            .age = lsaHdr.getAge()
        };

        auto record = rtr.lsus().get(key);
        if (!record.has_value()) continue;

        if (record->record->header == lsa)
            rtr.lsus().erase(key);
    }
}

void PacketDispatcherV2::processLSRequest(PacketDispatcher::HeaderInfo& info)
{
    Ospfv2LSRHeader hdr;
    hdr.setBuffer(info.payload);

    // Varify only 12 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 12 != 0)
        return;

    // Process request
    std::vector<std::pair<FloodInfo, LsaRecordRef>> records;
    auto& lsdb = iface.getArea().lsdb();
    while (info.offset < info.payloadSize)
    {
        Ospfv2LSRHeader lsrHdr;
        lsrHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv2LSRHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(static_cast<uint16_t>(lsrHdr.getType()), lsrHdr.getLsID(), lsrHdr.getAdvRouter());
        if (LsaRecord* record = lsdb.find(key); record)
        {
            if (key.lsaType < OSPFV2_LSA_OPAQUE_LINK || iface.opaqueEnabled.load(std::memory_order_relaxed))
                records.push_back({{FloodReason::UPDATE}, {key, *record}});
        }
    }

    sendReliableLSUpdate(info.neighbor, records);
}

void PacketDispatcherV2::processLSUpdate(PacketDispatcher::HeaderInfo& info)
{
    auto& area = iface.getArea();
    if (info.payloadSize < 4)
        return;

    if (info.neighbor->getState() < Neighbor::State::EXCHANGE)
        return;

    uint32_t lsuSize = readU32(info.payload);
    uint32_t routerId = iface.getProcess().getRouterId();

    info.offset += 4;

    std::vector<LsaRecordRef> acks;

    for (int i = 0; i < static_cast<int>(lsuSize); i++)
    {
        Ospfv2LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        size_t off = info.offset;

        off += Ospfv2LSAHeader::fixedSize;
        if (off > info.payloadSize) 
            return;

        bool selfOrigin = lsaHdr.getAdvRouter() == routerId;

        LsaKey key = {
            lsaHdr.getType(),
            lsaHdr.getLsID(),
            lsaHdr.getAdvRouter()
        };
        LsaHeader hdr = {
            lsaHdr.getSeqNumber(),
            lsaHdr.getChecksum(),
            lsaHdr.getLen(),
            lsaHdr.getAge(),
                lsaHdr.getOptions()
        };

        uint16_t bodyLen = hdr.length - Ospfv2LSAHeader::fixedSize;
        auto body = buildLsaBody(static_cast<uint8_t>(key.lsaType), info.payload + off, bodyLen);

        if (hdr.length < Ospfv2LSAHeader::fixedSize)
            return;
        if (info.offset + hdr.length > info.payloadSize)
            return;

        info.offset += hdr.length;

        if (!body.has_value()) continue;

        auto calc = runLsaCalculations<PolicyV2>(hdr, key, body.value());
        bool checksumValid = calc.checksum == hdr.checksum;

        IncomingLsaContext context{
            key,
            hdr,
            checksumValid,
            selfOrigin,
            {},
            iface.interfaceId,
            info.neighbor->routerID
        };

        auto result = area.processLsa<PolicyV2>(context, body.value());
        if (result.has_value() && result->decision.shouldAck)
            acks.push_back({context.key, *result->record});
    }

    if (!acks.empty())
        sendLSAck(*info.neighbor, acks);
}

void PacketDispatcherV2::processLLSDataBlock(PacketDispatcher::HeaderInfo& info)
{
    info.offset += info.authSize; // Only structure that needs to include auth size

    if (info.offset != info.payloadSize)
        return;

    uint8_t* llsBase = info.payload + info.payloadSize;

    if (info.packetSize < info.offset + 4)
        return;

    uint16_t llsLen = readU16(llsBase + 2);
    if (llsLen < 4 || info.offset + llsLen > info.packetSize)
        return;

    bool authEnabled = info.authType == static_cast<uint8_t>(AuthType::CRYPTO) &&
        iface.authKey.has_value() && iface.authKeyId.has_value();

    uint32_t extension{0};
    size_t offset = 4;

    auto parseExtensionTLV = [&](uint16_t size) noexcept
    {
        if (size != 4 || offset + 4 > llsLen) 
            return false;

        extension = readU32(llsBase + offset);
        offset += 4;
        return true;
    };

    auto parseAuthTLV = [&](uint16_t size) noexcept
    {
        if (!authEnabled || size != 20 || offset + 20 > llsLen)
            return false;

        const uint32_t seq = readU32(llsBase + offset);
        if (info.neighbor->lastAuthSeq.load(std::memory_order_relaxed) > seq)
            return false;

        uint8_t key[16]{};
        writeU128(key, iface.authKey.value());

        uint8_t digest[16]{};
        Authentication::generateHMAC(digest, llsBase, llsLen - 16, key, 16, Authentication::HmacType::MD5);

        if (std::memcmp(digest, llsBase + llsLen - 16, 16) != 0)
            return false;

        offset += 20;
        return true;
    };

    while (offset + 4 < llsLen)
    {
        uint16_t type = readU16(llsBase + offset);
        uint16_t size = readU16(llsBase + offset + 2);
        offset += 4;

        switch (type)
        {
            case 0x0001:
                if (!parseExtensionTLV(size))
                    return;
                break;
            case 0x0002:
                if (!parseAuthTLV(size))
                    return;
                break;
            default:
                if (offset + size > llsLen)
                    return;
                offset += size;
                break;
        }
    }

    if (!authEnabled) // Validate Checksum if no auth
    {
        ChecksumFletcher check;
        check.addBytes(llsBase + 2, llsLen - 2);
        if (check.finalize() != readU16(llsBase))
            return;
    }

    // TODO: process extension
}

bool PacketDispatcherV2::processOspfSimpleAuthentication(const Ospfv2Header& hdr)
{
    auto& secretVal = baseConfigs->get<Config::OspfInterfaceBase::AUTHENTICATION_KEY>();
    if (!secretVal.hasValue()) return true; // Auth not fully enabled.
    if (hdr.getAuthType() != static_cast<uint16_t>(AuthType::SIMPLE))
        return false;
    uint8_t secret[8] = {};
    writeU64(secret, secretVal.load());
    return std::memcmp(hdr.getAuthentication(), secret, 8) == 0;
}

bool PacketDispatcherV2::processOspfCryptoAuthentication(HeaderInfo& info, const Ospfv2Header& hdr)
{
    auto& id = iface.authKeyId;
    auto& key = iface.authKey;
    if (!id.has_value() || !key.has_value())
        return true; // Auth not fully enabled
    if (info.packetSize < hdr.getPacketLen() + 16)
        return false; // No size for proper auth.
    if (hdr.getAuthType() != static_cast<uint16_t>(AuthType::CRYPTO))
        return false;
    uint8_t* auth = hdr.getAuthentication();
    if (auth[2] != key.value())
        return false;
    if (auth[3] != 16)
        return false;
    if (uint32_t seq = readU32(auth + 4); seq > info.neighbor->lastAuthSeq.load(std::memory_order_relaxed))
        info.neighbor->lastAuthSeq.store(seq, std::memory_order_release);
    else return false;
    
    uint8_t authSecret[16];
    writeU128(authSecret, key.value());
    uint8_t authDigest[16];
    Authentication::generateHMAC(authDigest, hdr.buffer, hdr.getPacketLen(), authSecret, 16, Authentication::HmacType::MD5);
    info.authSize = 16;
    return std::memcmp(authDigest, hdr.buffer + hdr.getPacketLen(), 16) == 0;
}

std::optional<LsaBody> PacketDispatcherV2::buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len)
{
    switch (type)
    {
        case OSPFV2_LSA_ROUTER:
            return RouterLsaV2::build(buf, len);
        case OSPFV2_LSA_NETWORK:
            return NetworkLsaV2::build(buf, len);
        case OSPFV2_LSA_SUM_NET:
            return SummaryNetworkLsa::build(buf, len);
        case OSPFV2_LSA_SUM_ASBR:
            return SummaryRouterLsa::build(buf, len);
        case OSPFV2_LSA_EXTERNAL:
        case OSPFV2_LSA_NSSA:
            return ExternalLsaV2::build(buf, len);
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
            return std::nullopt;
    }
    return std::nullopt;
}
}
