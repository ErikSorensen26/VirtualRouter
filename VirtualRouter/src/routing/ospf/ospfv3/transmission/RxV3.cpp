// Ospfv3Rx.cpp

#include "PacketDispatcherV3.h"
#include "ospf/OspfProcess.h"
#include "ospf/interface/OspfInterface.h"
#include "ospf/interface/InterfaceTimers.h"
#include "ospf/neighbor/Neighbor.h"
#include "ospf/area/Area.h"
#include "ospf/FlagManager.hpp"

#include "packet/headers/embedded/ospf/Ospfv3HelloHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3DBDHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3LSAHeader.hpp"
#include "packet/headers/embedded/ospf/Ospfv3LSRHeader.hpp"

#include "ospf/ospfv2/database/OpaqueLsaV2.hpp"
#include "ospf/ospfv3/database/IntraAreaPrefixLsa.hpp"
#include "ospf/transmission/OspfFletcher.hpp"

namespace routing::ospf
{
// Recomputes the checksum over bytes [2, len) (skipping the 2-byte Age field) with the
// 2-byte checksum field (at LSA offset 16-17) treated as zero — matching how the
// checksum was originally computed — then compares against the value on the wire.
static bool verifyOspfFletcher(const uint8_t* lsa, uint16_t len)
{
    if (len < 20) return false;
    ChecksumFletcher check;
    check.addBytes(lsa + 2, 14);   // type..seqNum (LSA offset 2..15)
    check.addU16(0);               // checksum field (LSA offset 16..17), treated as zero
    check.addBytes(lsa + 18, len - 18); // length..end of body (LSA offset 18..len-1)
    return check.finalize() == utils::readU16(lsa + 16);
}

void PacketDispatcherV3::handleIncoming(const packet::Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast)
{
    types::IPAddress neigIp(neighborIp, iface.area.process.af);
    uint32_t rid = ospfHeader.getRouterID();

    // Check if 
    auto ntype = getIfaceConfigs().get<config::OspfInterface::NETWORK>().load();
    if (multicast && (ntype == config::ospf::NetworkType::NON_BROADCAST || ntype == config::ospf::NetworkType::POINT_TO_MULTIPOINT))
        return;

    // Check passive
    if (getIfaceConfigs().get<config::OspfInterface::PASSIVE>().load())
        return;

    // Validate version
    if (ospfHeader.getVersion() != OSPFV3_VERSION)
        return;

    if (ospfHeader.getAreaID() != iface.getAreaId())
        return;

    // Validate size
    size_t packetSize = packet::Ospfv3Header::fixedSize + ospfHeader.getTrail().size();
    if (ospfHeader.getPacketLen() > packetSize) return;

    HeaderInfo info(ospfHeader.getTrail().data(), packetSize,
        static_cast<uint16_t>(ospfHeader.getPacketLen() - packet::Ospfv3Header::fixedSize), 0, neigIp, rid);
    info.neighbor = getNTable().lookup(rid);

    {
        // Process Checksum
        ChecksumFletcher check;
        check.addBytes(ospfHeader.buffer, 8); // Up to checksum field
        check.addBytes(ospfHeader.buffer + 10, ospfHeader.getPacketLen() - 10); // To end of header
        uint16_t checksum = check.finalize();
        if (checksum != ospfHeader.getChecksum())
            return; // Invalid checksum
    }
    // RFC 5340 §4.4.1: discard packets sourced by this router itself
    if (ospfHeader.getRouterID() == iface.area.process.getRouterId())
        return;

    // Discard unrecognised packet types (valid range: 1–5)
    if (ospfHeader.getType() < 1 || ospfHeader.getType() > 5)
        return;

    // RFC 5340 §4.4.1: instance ID must match the interface's configured instance
    if (ospfHeader.getInstanceID() != getIfaceBaseConfigs().get<config::OspfInterfaceBase::INSTANCE_ID>().load())
        return;

    if (ospfHeader.getType() == OSPFV3_TYPE_HELLO)
    {
        if (info.neighbor && info.neighbor->unicast == multicast)
            return;
        processHello(info, !multicast);
    }
    else if (info.neighbor)
    {
        switch (ospfHeader.getType())
        {
            case OSPFV3_TYPE_DATABASE_DESCRIPTION:
                processDBD(info);
                break;
            case OSPFV3_TYPE_LINK_STATE_ACK:
                processLSAck(info);
                break;
            case OSPFV3_TYPE_LINK_STATE_REQUEST:
                processLSRequest(info);
                break;
            case OSPFV3_TYPE_LINK_STATE_UPDATE:
                processLSUpdate(info);
                break;
        }
    }
}

void PacketDispatcherV3::processHello(PacketDispatcher::HeaderInfo& info, bool unicast)
{
    packet::Ospfv3HelloHeader hdr;
    hdr.setBuffer(info.payload);

    auto& ifaceConfigs = getIfaceConfigs();

    info.offset += packet::Ospfv3HelloHeader::fixedSize;
    if (info.offset > info.payloadSize)
        return;

    // Validate timers — if mismatch, tear down an existing neighbor; for unknown neighbors just drop
    if (hdr.getHelloInterval() != getHelloInterval() || hdr.getDeadInterval() != getDeadInterval())
    {
        if (info.neighbor)
            info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    auto ntype = ifaceConfigs.get<config::OspfInterface::NETWORK>().load();
    bool multiAccess = ntype == config::ospf::NetworkType::BROADCAST || ntype == config::ospf::NetworkType::NON_BROADCAST;

    // Create neighbor if first Hello from this router
    if (!info.neighbor)
    {
        info.neighbor = ntable.createNeighbor(info.rid, info.neighborIp, unicast);
        info.neighbor->setState(Neighbor::State::INIT);
    }
    else if (unicast)
    {
        auto state = info.neighbor->getState();
        if (state == Neighbor::State::DOWN || state == Neighbor::State::ATTEMPT)
            info.neighbor->setState(Neighbor::State::INIT);
    }

    // Validate options — neighbor is guaranteed non-null from here on
    if (info.neighbor->getState() != Neighbor::State::FULL && !processOptions<PolicyV3>(static_cast<uint32_t>(hdr.getOptions()), *info.neighbor))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    if (ntype == config::ospf::NetworkType::BROADCAST || ntype == config::ospf::NetworkType::NON_BROADCAST)
    {
        uint8_t* neighborList = info.payload + packet::Ospfv3HelloHeader::fixedSize;
        size_t listSize = info.payloadSize - packet::Ospfv3HelloHeader::fixedSize;
        if (listSize % 4 != 0) return;

        // Find RID
        bool ridFound = false;
        for (size_t i = 0; i < listSize; i += 4)
        {
            if (utils::readU32(neighborList + i) == iface.area.process.getRouterId())
            {
                ridFound = true;
                break;
            }
        }

        // Handle two way
        if (ridFound)
        {
            // Bidirectional comminication established
            if (info.neighbor->getState() == Neighbor::State::INIT)
                info.neighbor->setState(Neighbor::State::TWOWAY);
        }
        else if (info.neighbor->getState() > Neighbor::State::INIT)
        {
            // No bidirectional communication established
            info.neighbor->setState(Neighbor::State::INIT);
        }
    }
    else if (info.neighbor->getState() == Neighbor::State::INIT)
    {
        info.neighbor->setState(Neighbor::State::TWOWAY);
    }

    getTmgr().startInactiveTimer(*info.neighbor);

    // Update neighbor variables
    if (info.neighbor->priority.load(std::memory_order_relaxed) != hdr.getRouterPriority())
        info.neighbor->priority.store(hdr.getRouterPriority(), std::memory_order_release);

    if (multiAccess)
    {
        // Read new values from the Hello
        const uint8_t  newPriority = hdr.getRouterPriority();
        const uint32_t newDr       = hdr.getDrID();
        const uint32_t newBdr      = hdr.getBdrID();

        // Update neighbor-advertised DR/BDR and priority from the Hello
        info.neighbor->priority.store(newPriority, std::memory_order_relaxed);
        info.neighbor->dr.store(newDr, std::memory_order_relaxed);
        info.neighbor->bdr.store(newBdr, std::memory_order_relaxed);

        uint32_t currentDr = iface.getDrRid();
        uint32_t currentBdr = iface.getDrRid();

        const bool election = (newPriority == 0 &&
            (info.neighbor->routerID == currentBdr ||
             info.neighbor->routerID == currentDr)) ||
            (info.neighbor->getState() == Neighbor::State::TWOWAY &&
            ((iface.getDrRid() == 0) ||
            (iface.getBdrRid() == 0)));

        if (election)
            runDrElection();
    }
}

void PacketDispatcherV3::processDBD(PacketDispatcher::HeaderInfo& info)
{
    auto state = info.neighbor->getState();
    if (state < Neighbor::State::EXSTART) return;

    packet::Ospfv3DBDHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += packet::Ospfv3DBDHeader::fixedSize;
    if (info.offset > info.payloadSize) 
        return;

    // Varify only 20 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 20 != 0)
        return;

    uint32_t options = hdr.getOptions();
    if (!processOptions<PolicyV3>(options, *info.neighbor))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    // Verify MTU
    if (getIfaceConfigs().get<config::OspfInterface::MTU_IGNORE>().load() && info.neighbor->mtu != hdr.getMtu())
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    bool ack = hdr.getSeqNum() == info.neighbor->currentSeq.load(std::memory_order_relaxed);
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

        Neighbor::Role role = info.neighbor->routerID > iface.area.process.getRouterId()
            ? Neighbor::Role::SLAVE
            : Neighbor::Role::MASTER;
        info.neighbor->setRole(role);

        if (role == Neighbor::Role::SLAVE)
            info.neighbor->currentSeq.store(hdr.getSeqNum());

        else if (state != Neighbor::State::EXSTART)
            // Move to EXSTART, will send init either acking or setting initial sequence.
            info.neighbor->setState(Neighbor::State::EXSTART);

        if (role == Neighbor::Role::SLAVE) // SLAVE
        {
            // Ack MASTERs init, move to EXCHANGE, and await MASTER.
            sendInitDbd(*info.neighbor);
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
            packet::Ospfv3LSAHeader lsaHdr;
            lsaHdr.setBuffer(info.payload + info.offset);

            info.offset += packet::Ospfv3LSAHeader::fixedSize;
            if (info.offset > info.payloadSize) 
                return;

            LsaKey key(lsaHdr.getType(), lsaHdr.getLsId(), lsaHdr.getAdvRouter());

            LsaHeader lsa = {
                .sequence = lsaHdr.getSeqNumber(),
                .checksum = lsaHdr.getChecksum(),
                .length = lsaHdr.getLen(),
                .age = lsaHdr.getAge()
            };

            if (compareLSASummary(lsa, key))
                rtr.lsrs().add(key, key);
        }

        bool peerHasMore = hdr.getFlagM();

        // Slave must ack every Master DBD, including the final empty one.
        // Master only needs to reply while it still has more to describe.
        if (info.neighbor->getRole() == Neighbor::Role::SLAVE || info.neighbor->currentDbd.has_value())
            sendDbd(*info.neighbor);

        if (!peerHasMore && !info.neighbor->currentDbd.has_value())
        {
            // Both sides have fully described their databases.
            info.neighbor->setState(Neighbor::State::LOADING);
        }
    }

    if (AreaFlagManager::getLBit(options))
        processLLSDataBlock(info);
}

void PacketDispatcherV3::processLSAck(HeaderInfo& info)
{
    // Process ACKs.
    auto& rtr = info.neighbor->getRtr();
    while (info.offset < info.payloadSize)
    {
        packet::Ospfv3LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        info.offset += packet::Ospfv3LSAHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(lsaHdr.getType(), lsaHdr.getLsId(), lsaHdr.getAdvRouter());

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

void PacketDispatcherV3::processLSRequest(PacketDispatcher::HeaderInfo& info)
{
    packet::Ospfv3LSRHeader hdr;
    hdr.setBuffer(info.payload);

    // Varify only 12 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 12 != 0)
        return;

    // Process request
    std::vector<std::pair<FloodInfo, LsaRecordRef>> records;
    auto& lsdb = getLsdb();
    while (info.offset < info.payloadSize)
    {
        packet::Ospfv3LSRHeader lsrHdr;
        lsrHdr.setBuffer(info.payload + info.offset);

        info.offset += packet::Ospfv3LSRHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(static_cast<uint16_t>(lsrHdr.getType()), lsrHdr.getLsID(), lsrHdr.getAdvRouter());
        if (const LsaRecord* record = lsdb.find(key); record)
        {
            records.push_back({{FloodReason::UPDATE}, {key, *record}});
        }
    }

    sendReliableLsu(info.neighbor, records);
}

void PacketDispatcherV3::processLSUpdate(PacketDispatcher::HeaderInfo& info)
{
    if (info.payloadSize < 4)
        return;

    if (info.neighbor->getState() < Neighbor::State::EXCHANGE)
        return;

    uint32_t lsuSize = utils::readU32(info.payload);
    uint32_t routerId = iface.area.process.getRouterId();

    info.offset += 4;

    std::vector<LsaRecordRef> acks;

    for (int i = 0; i < static_cast<int>(lsuSize); i++)
    {
        packet::Ospfv3LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        size_t off = info.offset;

        off += packet::Ospfv3LSAHeader::fixedSize;
        if (off > info.payloadSize) 
            return;

        bool selfOrigin = lsaHdr.getAdvRouter() == routerId;

        LsaKey key = {
            lsaHdr.getType(),
            lsaHdr.getLsId(),
            lsaHdr.getAdvRouter()
        };
        LsaHeader hdr = {
            lsaHdr.getSeqNumber(),
            lsaHdr.getChecksum(),
            lsaHdr.getLen(),
            lsaHdr.getAge(),
        };

        if (hdr.length < packet::Ospfv3LSAHeader::fixedSize)
            return;
        if (info.offset + hdr.length > info.payloadSize)
            return;

        // Validate raw wire Fletcher checksum (RFC 5340 §A.3) before parsing body
        bool checksumValid = verifyOspfFletcher(info.payload + info.offset, hdr.length);

        info.offset += hdr.length;

        uint16_t bodyLen = hdr.length - packet::Ospfv3LSAHeader::fixedSize;
        auto body = buildLsaBody(key.lsaType, info.payload + off, bodyLen);

        if (!body.has_value()) continue;

        IncomingLsaContext context{
            key,
            hdr,
            checksumValid,
            selfOrigin,
            {FloodReason::UPDATE},
            iface.interfaceId,
            info.neighbor->routerID
        };

        auto result = processLsa<PolicyV3>(context, body.value());
        if (result.has_value() && result->decision.shouldAck)
            acks.push_back({context.key, *result->record});
    }

    if (!acks.empty())
        sendLsAck(*info.neighbor, acks);
}

void PacketDispatcherV3::processLLSDataBlock(PacketDispatcher::HeaderInfo& info)
{
    info.offset += info.authSize; // Only structure that needs to include auth size

    if (info.offset != info.payloadSize)
        return;

    uint8_t* llsBase = info.payload + info.payloadSize;

    if (info.packetSize < info.offset + 4)
        return;

    uint16_t llsLen = utils::readU16(llsBase + 2);
    if (llsLen < 4 || info.offset + llsLen > info.packetSize)
        return;

    uint32_t extension{0};
    size_t offset = 4;

    auto parseExtensionTLV = [&](uint16_t size) noexcept
    {
        if (size != 4 || offset + 4 > llsLen) 
            return false;

        extension = utils::readU32(llsBase + offset);
        offset += 4;
        return true;
    };

    while (offset + 4 < llsLen)
    {
        uint16_t type = utils::readU16(llsBase + offset);
        uint16_t size = utils::readU16(llsBase + offset + 2);
        offset += 4;

        switch (type)
        {
            case 0x0001:
                if (!parseExtensionTLV(size))
                    return;
                break;
            default:
                if (offset + size > llsLen)
                    return;
                offset += size;
                break;
        }
    }

    ChecksumFletcher check;
    check.addBytes(llsBase + 2, llsLen - 2);
    if (check.finalize() != utils::readU16(llsBase))
        return;

    // Trigger DC integrity scan so flood-reduction state stays current
    runDCIntegrityScan();
}

std::optional<LsaBody> PacketDispatcherV3::buildLsaBody(uint16_t type, const uint8_t* buf, uint16_t len)
{
    switch (type)
    {
        case OSPFV3_LSA_ROUTER:
            return RouterLsaV3::build(buf, len);
        case OSPFV3_LSA_NETWORK:
            return NetworkLsaV3::build(buf, len);
        case OSPFV3_LSA_INTER_AREA_PREFIX:
            return InterAreaPrefixLsa::build(buf, len);
        case OSPFV3_LSA_INTER_AREA_ROUTER:
            return InterAreaRouterLsa::build(buf, len);
        case OSPFV3_LSA_LINK:
            return LinkLsa::build(buf, len);
        case OSPFV3_LSA_INTRA_AREA_PREFIX:
            return IntraAreaPrefixLsa::build(buf, len);
        case OSPFV3_LSA_AS_EXTERNAL:
        case OSPFV3_LSA_NSSA_EXTERNAL:
            return ExternalLsaV3::build(buf, len);
        default:
        {
            // RFC 5340: if U-bit (bit 15) is set, store and flood as unknown
            // using the scope indicated by bits 14-13. Otherwise discard.
            if (type & 0x8000)
            {
                // Store as raw opaque payload — reuse OpaqueLsaV2 for unknown V3 LSAs
                return OpaqueLsaV2::build(type, buf, len);
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}
} // namespace routing
