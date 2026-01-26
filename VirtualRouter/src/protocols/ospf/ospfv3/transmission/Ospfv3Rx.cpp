// Ospfv3Rx.cpp

#include "PacketDispatcherV3.h"
#include <OspfProcess.h>
#include <OspfInterface.h>
#include <OspfInterfaceTimers.h>

#include <Ospfv3HelloHeader.hpp>
#include <Ospfv3DBDHeader.hpp>
#include <Ospfv3LSAHeader.hpp>
#include <Ospfv3LSRHeader.hpp>
#include <OspfNeighbor.h>
#include <OspfTopology.h>
#include <OspfArea.h>
#include <OspfFlagManager.h>
#include <Encryption.hpp>

#include <Interface.h>
#include <InterfaceConfigs.h>

namespace OSPF
{
Config::OspfInterfaceBaseRegistry& PacketDispatcherV3::getBaseConfigs()
{
    return baseConfigs.get();
}

void PacketDispatcherV3::handleIncoming(const Ospfv3Header& ospfHeader, const uint8_t* neighborIp, bool multicast)
{
    IPAddress neigIp(neighborIp, iface.process.getAF());
    uint32_t rid = ospfHeader.getRouterID();

    // Check passive
    if (iface.getConfigs().get<Config::OspfInterface::PASSIVE>().load())
        return;

    // Validate version
    if (ospfHeader.getVersion() != OSPFV3_VERSION)
        return;

    if (ospfHeader.getAreaID() != iface.getAreaId())
        return;

    // Validate size
    size_t packetSize = Ospfv3Header::fixedSize + ospfHeader.getTrail().size();
    if (ospfHeader.getPacketLen() > packetSize) return;

    HeaderInfo info(ospfHeader.getTrail().data(), packetSize, ospfHeader.getPacketLen(), neigIp, rid);
    info.neighbor = iface.getNTable().lookup(rid);

    {
        // Process Checksum
        ChecksumFletcher check;
        check.addBytes(ospfHeader.buffer, 8); // Up to checksum field
        check.addBytes(ospfHeader.buffer + 10, ospfHeader.getPacketLen() - 10); // To end of header
        uint16_t checksum = check.finalize();
        if (checksum != ospfHeader.getChecksum())
            return; // Invalid checksum
    }
    // TODO header stuff

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

bool PacketDispatcherV3::processOptions(uint32_t options, Neighbor& nbr)
{
    auto& flags = iface.getFlags();
    auto& areaFlags = iface.getArea().getFlags();

    if (iface.demandCircuit.load(std::memory_order_relaxed) == OspfInterface::DcDecision::UNDECIDED)
    {
        if (InterfaceFlagManager::getDemandCircuits(options) && flags.getDemandCircuits() &&
            iface.getConfigs().get<Config::OspfInterface::NETWORK>().load() == NetworkType::POINT_TO_POINT)
            iface.demandCircuit.store(OspfInterface::DcDecision::ENABLED, std::memory_order_release);
        else
            iface.demandCircuit.store(OspfInterface::DcDecision::DISABLED, std::memory_order_release);
    }
    if (auto r = AreaFlagManager::getRouterBit(options); r != nbr.isTransit.load(std::memory_order_relaxed))
        nbr.isTransit.store(r, std::memory_order_release);
    if (areaFlags.getExternalRouting() != AreaFlagManager::getExternalRouting(options))
        return false;
    if (areaFlags.getNssa() != AreaFlagManager::getNssa(options))
        return false;
    return true;
}

void PacketDispatcherV3::processHello(PacketDispatcher::HeaderInfo& info, bool unicast)
{
    Ospfv3HelloHeader hdr;
    hdr.setBuffer(info.payload);

    auto& ifaceConfigs = iface.getConfigs();

    info.offset += Ospfv3HelloHeader::fixedSize;
    if (info.offset > info.payloadSize)
        return;

    // Validate timers
    if (hdr.getHelloInterval() != ifaceConfigs.get<Config::OspfInterface::HELLO_INTERVAL>().load() ||
        hdr.getDeadInterval() != ifaceConfigs.get<Config::OspfInterface::DEAD_INTERVAL>().load())
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    if (info.neighbor->getState() != Neighbor::State::FULL && !processOptions(static_cast<uint32_t>(hdr.getOptions()), *info.neighbor))
    {
        info.neighbor->setState(Neighbor::State::DOWN);
        return;
    }

    auto ntype = ifaceConfigs.get<Config::OspfInterface::NETWORK>().load();
    bool multiAccess = ntype == NetworkType::BROADCAST || ntype == NetworkType::NON_BROADCAST;

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
        uint8_t* neighborList = info.payload + Ospfv3HelloHeader::fixedSize;
        size_t listSize = info.payloadSize - Ospfv3HelloHeader::fixedSize;
        if (listSize % 4 != 0) return;

        // Find RID
        bool ridFound = false;
        for (size_t i = 0; i < listSize; i += 4)
        {
            if (readU32(neighborList + i) == iface.process.getRouterId())
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

    info.neighbor->markHeard();
    iface.getTimers().startInactiveTimer(*info.neighbor);

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

void PacketDispatcherV3::processDBD(PacketDispatcher::HeaderInfo& info)
{
    OspfArea& area = iface.getArea();

    auto state = info.neighbor->getState();
    if (state < Neighbor::State::EXSTART) return;

    Ospfv3DBDHeader hdr;
    hdr.setBuffer(info.payload);

    info.offset += Ospfv3DBDHeader::fixedSize;
    if (info.offset > info.payloadSize) 
        return;

    // Varify only 20 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 20 != 0)
        return;

    uint32_t options = hdr.getOptions();
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

        Neighbor::Role role = info.neighbor->routerID > iface.process.getRouterId()
            ? Neighbor::Role::MASTER
            : Neighbor::Role::SLAVE;
        info.neighbor->setRole(role);

        if (role == Neighbor::Role::SLAVE)
            info.neighbor->currentSeq.store(hdr.getSeqNum());

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
            Ospfv3LSAHeader lsaHdr;
            lsaHdr.setBuffer(info.payload + info.offset);

            info.offset += Ospfv3LSAHeader::fixedSize;
            if (info.offset > info.payloadSize) 
                return;

            LsaKey key(lsaHdr.getType(), lsaHdr.getLsId(), lsaHdr.getAdvRouter());

            LsaHeader lsa = {
                .sequence = lsaHdr.getSeqNumber(),
                .checksum = lsaHdr.getChecksum(),
                .length = lsaHdr.getLen(),
                .age = lsaHdr.getAge()
            };

            if (area.compareLSASummary(lsa, key))
                rtr.addLsr(key);
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

    if (AreaFlagManager::getLBit(options))
        processLLSDataBlock(info);
}

void PacketDispatcherV3::processLSAck(HeaderInfo& info)
{
    // Process ACKs.
    auto& rtr = info.neighbor->getRtr();
    while (info.offset < info.payloadSize)
    {
        Ospfv3LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv3LSAHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(lsaHdr.getType(), lsaHdr.getLsId(), lsaHdr.getAdvRouter());

        LsaHeader lsa = {
            .sequence = lsaHdr.getSeqNumber(),
            .checksum = lsaHdr.getChecksum(),
            .length = lsaHdr.getLen(),
            .age = lsaHdr.getAge()
        };

        auto record = rtr.getLsu(key);
        if (!record.has_value()) continue;

        if (record->record->header == lsa)
            rtr.eraseLsu(key);
    }
}

void PacketDispatcherV3::processLSRequest(PacketDispatcher::HeaderInfo& info)
{
    Ospfv3LSRHeader hdr;
    hdr.setBuffer(info.payload);

    // Varify only 12 byte lsa blocks exist
    if ((info.payloadSize - info.offset) % 12 != 0)
        return;

    // Process request
    std::vector<std::pair<FloodInfo, LsaRecordRef>> records;
    auto& lsdb = iface.getArea().lsdb();
    while (info.offset < info.payloadSize)
    {
        Ospfv3LSRHeader lsrHdr;
        lsrHdr.setBuffer(info.payload + info.offset);

        info.offset += Ospfv3LSRHeader::fixedSize;
        if (info.offset > info.payloadSize) 
            return;

        LsaKey key(static_cast<uint16_t>(lsrHdr.getType()), lsrHdr.getLsID(), lsrHdr.getAdvRouter());
        if (LsaRecord* record = lsdb.find(key); record)
        {
            records.push_back({{FloodReason::UPDATE}, {key, *record}});
        }
    }

    sendReliableLSUpdate(info.neighbor, records);
}

void PacketDispatcherV3::processLSUpdate(PacketDispatcher::HeaderInfo& info)
{
    auto& topology = *iface.topology.load(std::memory_order_relaxed);
    auto& area = iface.getArea();
    if (info.payloadSize < 4)
        return;

    if (info.neighbor->getState() < Neighbor::State::EXCHANGE)
        return;

    uint32_t lsuSize = readU32(info.payload);
    uint32_t routerId = iface.process.getRouterId();

    info.offset += 4;

    std::vector<LsaRecordRef> acks;

    for (int i = 0; i < static_cast<int>(lsuSize); i++)
    {
        Ospfv3LSAHeader lsaHdr;
        lsaHdr.setBuffer(info.payload + info.offset);

        size_t off = info.offset;

        off += Ospfv3LSAHeader::fixedSize;
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

        uint16_t bodyLen = hdr.length - Ospfv3LSAHeader::fixedSize;
        auto body = buildLsaBody(static_cast<uint8_t>(key.lsaType), info.payload + off, bodyLen);

        if (hdr.length < Ospfv3LSAHeader::fixedSize)
            return;
        if (info.offset + hdr.length > info.payloadSize)
            return;

        info.offset += hdr.length;

        if (!body.has_value()) continue;

        auto calc = runLsaCalculations<PolicyV3>(hdr, key, body.value());
        bool checksumValid = calc.checksum == hdr.checksum;

        IncomingLsaContext context{
            key,
            hdr,
            checksumValid,
            selfOrigin,
            {FloodReason::UPDATE},
            iface.interfaceId,
            info.neighbor->routerID
        };

        OspfArea::Result result = area.processLsa<PolicyV3>(context, body.value());
        if (result.decision.shouldAck)
            acks.push_back({context.key, *result.record});
    }

    if (!acks.empty())
        sendLSAck(*info.neighbor, acks);

    topology.flood<PolicyV3>();
}

void PacketDispatcherV3::processLLSDataBlock(PacketDispatcher::HeaderInfo& info)
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
                if (offset + size > llsLen)
                    return;
                offset += size;
                break;
        }
    }

    ChecksumFletcher check;
    check.addBytes(llsBase + 2, llsLen - 2);
    if (check.finalize() != readU16(llsBase))
        return;

    // TODO: process extension
}

std::optional<LsaBody> PacketDispatcherV3::buildLsaBody(uint8_t type, const uint8_t* buf, uint16_t len)
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
        case OSPFV3_LSA_AS_EXTERNAL:
        case OSPFV3_LSA_NSSA_EXTERNAL:
            return ExternalLsaV3::build(buf, len);
        default:
            return std::nullopt;
    }
    return std::nullopt;
}
}

